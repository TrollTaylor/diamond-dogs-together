// Project-authored adapter. Boundary provenance: docs/script-load-bridge.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
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
#include "equipment_observation_source.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/lab_status.hpp"
#include "mgscoop/client_reset.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_missions.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/partner_action_events.hpp"
#include "mgscoop/partner_driver.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_dm_patch.hpp"
#include "mgscoop/partner_upper_probe.hpp"
#include "mgscoop/partner_upper_end_observer.hpp"
#include "mgscoop/partner_upper_phase_observer.hpp"
#include "mgscoop/partner_upper_manual_idle.hpp"
#include "mgscoop/crash_probe.hpp"
#include "mgscoop/early_patches.hpp"
#include "mgscoop/world_link.hpp"
#include "mgscoop/world_receipt.hpp"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/worker_debug.hpp"
#include <charconv>
#include <cmath>
#include <cstdarg>

namespace {
namespace lab = mgscoop::lab;
HANDLE script_log = INVALID_HANDLE_VALUE;
volatile LONG64 entries = 0, returns = 0;
bool bootstrap_enabled = false;
bool archive_mounted = false;
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
DWORD allocation_thread = 0, mission_thread = 0;
DWORD cleanup_thread = 0;
mgscoop::MissionTermination termination_scope;
mgscoop::MissionInitialization initialization_scope;
mgscoop::equipment_allocation_probe::Authorization equipment_allocation_authorization;
mgscoop::equipment_stock_probe::Authorization stock46_authorization;
mgscoop::equipment_stock_contract::Snapshot stock46_before;
bool stock46_groups_authorized=false,stock46_groups_returned=false;
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
    return pp::WriteAnim(partner_addresses, partner::partner_index, terminating, act, stance, speed);
}
const char* PartnerWriteFaceEquip(void*, bool terminating, std::uint8_t value) noexcept {
    return pp::WriteFaceEquip(partner_addresses, partner::partner_index, terminating, value);
}
const char* PartnerSetStance(void*, bool terminating, std::uint8_t stance, std::uint8_t& psw, std::uint8_t& q30) noexcept {
    return pp::SetStance(partner_addresses, partner::partner_index, terminating, stance, psw, q30);
}
const char* PartnerRequestStance(void*, bool terminating, std::uint8_t stance) noexcept {
    return pp::RequestStance(partner_addresses, partner::partner_index, terminating, stance);
}
const char* PartnerApplyTransform(void*, bool terminating, const float (&pos)[4], const float (&quat)[4],
                                  mgscoop::PartnerReset reset) noexcept {
    return pp::ApplyTransform(partner_addresses, partner::partner_index, terminating, pos, quat, reset);
}
bool PartnerReadBody(void*, const pp::State& state, float (&pos)[4], float (&quat)[4]) noexcept {
    return pp::ReadBody(partner_addresses, state, partner::partner_index, pos, quat);
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
void PartnerResetInputFault(void*) noexcept { mgscoop::partner_input_hook::ResetFault(); }
void PartnerPublishArm(void*, const mgscoop::partner_input::Arm& arm) noexcept { mgscoop::partner_input_hook::PublishArm(arm); }
void PartnerDrainInputStats(void*, pd::InputStats& out) noexcept {
    mgscoop::partner_input_hook::Stats s{};
    mgscoop::partner_input_hook::DrainStats(s);
    out.window = s.window;
    for (unsigned i = 0; i < 8; ++i) out.holds[i] = s.holds[i];
    out.hold_count = s.hold_count;
    out.wrap = s.wrap;
}
bool PartnerInputHookLive(void*) noexcept { return mgscoop::partner_input_hook::PatchLive(game_base); }
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
bool PartnerDmClearPending(void*) noexcept {
    bool cleared = false;
    mgscoop::partner_player::ClearDmPending(partner_addresses, mission_terminating, cleared);
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
constexpr pd::Hooks partner_hooks{nullptr, PartnerLog, PartnerReadState, PartnerOrBits, PartnerClearBits, PartnerWriteSlot,
    PartnerForceExists, PartnerSetPolicy, PartnerReadAnim, PartnerSampleRemote, PartnerSubmitLocal, PartnerTakeTrigger,
    PartnerFocused, PartnerReadLooks, PartnerSubmitLook, PartnerSampleLook, PartnerWriteAnim, PartnerWriteFaceEquip,
    PartnerSetStance, PartnerRequestStance, PartnerApplyTransform, PartnerReadBody, PartnerCensus, PartnerFormatCensus,
    PartnerFormatPsProbe, PartnerInputTargets, PartnerFormatInputCensus, PartnerReadPad, PartnerSubmitInput, PartnerNetRunning,
    PartnerInstallInputHook, PartnerPublishArm, PartnerDrainInputStats, PartnerInputHookLive, PartnerDmTargets, PartnerReadDm,
    PartnerFormatDmCensus, PartnerFormatDmProbe, PartnerDmPatch, PartnerDmClearPending, PartnerResetInputFault,
    PartnerApplyPosition, PartnerReadVelocity, PartnerLatestInput, PartnerInstallWrap, PartnerInterpMs, PartnerDmMotionCount,
    PartnerSampleRemoteWithTurns};
// The mission of the current partner epoch (link-status.json "mission").
std::uint32_t partner_mission = 0;
// World sync (docs/world-sync-protocol.md): configured at install only with
// offline-v1, partner=1, net=1, link v2, redirected saves and lab.ini world>=1.
// World receipts are quiet (TX/RX/LOG log no entry/return lines; CFG does).
bool world_on = false;
thread_local bool world_quiet = false;
thread_local bool debug_quiet = false;
thread_local char debug_reply[320]{};
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

void PartnerMissionStart(std::uint32_t mission) noexcept {
    ClearUpperEndOwner();
    if (!partner_on) return;
    mgscoop::debug_menu::BeginMission(mission);
    pd::MissionStart(partner_driver, partner_hooks, mission);
    mgscoop::partner_net::ResetRemote();
    mgscoop::partner_net::SetEpoch(mission);
    partner_mission = mission;
    mgscoop::partner_net::PublishPartnerState(false, false, mission);
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, 0, PartnerCtxFlags());
}
void PartnerMissionEnd() noexcept {
    ClearUpperEndOwner();
    if (!partner_on) return;
    mgscoop::debug_menu::EndMission();
    pd::MissionEnd(partner_driver, partner_hooks);
    mgscoop::partner_net::ResetRemote();
    mgscoop::partner_net::SetEpoch(0);
    partner_mission = 0;
    mgscoop::partner_net::PublishPartnerState(false, false, 0);
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, 0, PartnerCtxFlags());
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
    mgscoop::world_link::Configure(world);
    mgscoop::partner_net::SetWorldHooks(mgscoop::world_link::Hooks());
    world_on = true;
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

// The per-frame tick. Returns the quiet reply, or nullptr to leave the literal
// (Lua then does nothing). The guarded native calls of the movement/stance
// experiments (T.vt+0x70/+0x68, the C0/A0 resets, SetStance, X.vt+0x10) run
// here, inside ObserveEntry with receipt_lock held. A callee that reached
// luaL_loadbuffer on this thread cannot re-enter ObserveEntry: the bridge
// (script_load_bridge.cpp Bridge) forwards any load issued while its selector
// runs unchanged, without calling the selector (thread_local selecting;
// tests/script_load_bridge_tests.cpp selector_reentry), so the
// non-recursive lock is never requested twice on one thread.
const char* HandlePartnerTick(const char* buffer, std::size_t size) noexcept {
    if (!OnRuntimeThread()) return nullptr;
    const std::uint64_t started_us = QpcMicros();
    const auto upper_body_epoch = partner_driver.mission.body_epoch;
    partner::Reply reply{};
    const pd::Clock clock{mgscoop::partner_net::NowMs(), GetTickCount64()};
    if (!pd::Tick(partner_driver, partner_hooks, mgscoop::GetLabConfig(), buffer, size, clock, mission_terminating, reply)) {
        ClearUpperEndOwner(); return nullptr;
    }
    ObserveRemoteActionEvents();
    if (!partner::FormatReply(reply, partner_reply, sizeof(partner_reply))) {
        ClearUpperEndOwner(); mgscoop::partner_upper_probe::Clear("reply_format"); return nullptr;
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
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, partner_driver.mission.realized ? 1u : 0u, PartnerCtxFlags());
    mgscoop::partner_net::PublishPartnerState(partner_driver.mission.realized, partner_driver.mission.parts_loaded, partner_mission);
    const std::uint64_t ended_us = QpcMicros();
    mgscoop::partner_net::RecordNativeMicros(Clamp32(ended_us >= started_us ? ended_us - started_us : 0));
    return partner_reply;
}

void ObserveEquipmentVtables() noexcept {
#ifdef MGS_LOCAL_OFFLINE_EQUIPMENT_TEST
    static std::uint32_t observed_epoch=UINT32_MAX;
    const auto& mission=partner_driver.mission;
    if(observed_epoch==mission.body_epoch || !offline_enabled || !lab_active || !lab::SavesRedirected() ||
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
    observed_epoch=mission.body_epoch;
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
    context.enabled=true;context.owner=OnRuntimeThread();
    context.mission=partner_mission;context.body_epoch=mission.body_epoch;
    context.base=reinterpret_cast<std::uintptr_t>(game_base);context.image_span=game_span;
    context.preload_queued=stock46_authorization.QueuedFor(partner_mission);
    unsigned short current_mission=0;
    context.offline_gates=offline_enabled && lab_active && lab::SavesRedirected() &&
        allocation_enabled && archive_mounted && host_source_selected && partner_on && partner_gate_ok &&
        !mission_terminating && !termination_scope.Active() && !gameplay_restore_pending &&
        !HasDynamiteSessionOwnership() && ReadMission(current_mission) &&
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

void PublishDebugSnapshot() noexcept {
    namespace debug = mgscoop::debug_menu;
    if (!debug::Enabled()) return;
    const auto now = mgscoop::partner_net::NowMs();
    static std::uint64_t published_ms = 0;
    if (now >= published_ms && now - published_ms < 100) return;
    published_ms = now;
    ObserveEquipmentVtables();
    ObserveStock46Writer();
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
    StringCchCopyA(snapshot.dm, ARRAYSIZE(snapshot.dm), mgscoop::PartnerDmName(settings.dm));
    StringCchCopyA(snapshot.look, ARRAYSIZE(snapshot.look), mgscoop::PartnerLookName(settings.look));
    if (mission.refusal) StringCchCopyA(snapshot.refusal, ARRAYSIZE(snapshot.refusal), mission.refusal);
    pp::State current{};
    if (pp::ReadState(partner_addresses, current) == pp::Refusal::none && current.vars_ok) {
        snapshot.pose_valid = true;
        for (unsigned i = 0; i < 3; ++i) snapshot.player_pos[i] = current.vars_pos[i];
        if (current.slots_ok) snapshot.player_yaw = current.slot[0][3] * 57.2957795f;
    }
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
    snapshot.position_error_m = std::sqrt(error);
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
    snapshot.mapped_clip = mission.dmmap.cur;
    snapshot.motion_count = MountedPlayerMotionCount();
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

bool SelectDebugReceipt(const char* buffer, std::size_t size, const char* name,
                        mgscoop::script_bridge::Replacement& replacement) noexcept {
    namespace debug = mgscoop::debug_menu;
    if (!debug::Enabled() || !buffer || !name || size >= 256 || std::strlen(name) != size ||
        std::memcmp(buffer, name, size) != 0) return false;
    constexpr char command[] = "return 'MGS_COOP_DEBUG_CMD_V1'";
    constexpr char log[] = "return 'MGS_COOP_DEBUG_LOG_V1' -- ";
    constexpr char state[] = "return 'MGS_COOP_DEBUG_STATE_V1' -- ";
    constexpr char partner_control[] = "return 'MGS_COOP_DEBUG_PARTNER_V1' -- ";
    constexpr char enemy[] = "return 'MGS_COOP_DEBUG_ENEMY_V1' -- ";
    const bool is_command = size == sizeof(command) - 1 && std::memcmp(buffer, command, size) == 0;
    const bool is_log = size >= sizeof(log) - 1 && std::memcmp(buffer, log, sizeof(log) - 1) == 0;
    const bool is_state = size >= sizeof(state) - 1 && std::memcmp(buffer, state, sizeof(state) - 1) == 0;
    const bool is_partner = size >= sizeof(partner_control) - 1 && std::memcmp(buffer, partner_control, sizeof(partner_control) - 1) == 0;
    const bool is_enemy = size >= sizeof(enemy) - 1 && std::memcmp(buffer, enemy, sizeof(enemy) - 1) == 0;
    if (!is_command && !is_log && !is_state && !is_partner && !is_enemy) return false;
    if (!OnRuntimeThread() && !(is_log || is_state ? OnTerminationThread() : false)) return false;
    const char* reply = "return nil";
    if (is_command) {
        PublishDebugSnapshot();
        mgscoop::worker_debug::PollOwner();
        unsigned short read_mission = 0;
        debug::Command pending{};
        if (!mission_terminating && ReadMission(read_mission) && read_mission == partner_mission &&
            mgscoop::SupportsPartnerBody(read_mission) && debug::Pop(pending)) {
            const auto snapshot = debug::GetSnapshot();
            StringCchPrintfA(debug_reply, ARRAYSIZE(debug_reply), "return {id=%u,text=\"%s\",host=%s,paired=%s,mission=%u}", pending.id, pending.text,
                snapshot.host ? "true" : "false", snapshot.world_paired ? "true" : "false", partner_mission);
            reply = debug_reply;
            LogFormat("debug_menu=dispatch; id=%u; text=%s; generation=%u\r\n", pending.id, pending.text, pending.generation);
        }
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
    } else {
        const char* cursor = buffer + (is_log ? sizeof(log) - 1 : sizeof(state) - 1);
        const char* end = buffer + size;
        std::uint32_t first = 0, second = 0;
        const auto a = std::from_chars(cursor, end, first);
        if (a.ec == std::errc{} && a.ptr < end && *a.ptr == ' ') {
            const auto b = std::from_chars(a.ptr + 1, end, second);
            if (b.ec == std::errc{}) {
                if (is_state && b.ptr == end && first <= 1 && second <= 255) debug::LuaState(first == 1, second);
                else if (is_log && b.ptr < end && *b.ptr == ' ' && second <= 1) {
                    char message[192]{};
                    const auto n = static_cast<std::size_t>(end - b.ptr - 1);
                    if (n < sizeof(message)) {
                        std::memcpy(message, b.ptr + 1, n);
                        debug::Result(first, second == 1, message);
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
    context.enabled=true;
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
        !HasDynamiteSessionOwnership() && ReadMission(mission) &&
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
    enabled=true;
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
            equipment_allocation_authorization.Begin(mission);
            stock46_authorization.Begin(mission); stock46_before={};
            mgscoop::equipment_stock_writer::BeginAllocation(mission);
            stock46_groups_authorized=stock46_groups_returned=false;
            equipment_observation_requested_ms=0; equipment_observation_mission=0;
        }
    } else if(equals(end)) {
        boundary="allocation_returned";
        if(OnAllocationThread()) { equipment_allocation_authorization.End(); stock46_authorization.SealAllocation(); mission_phase=MissionPhase::allocated; accepted=true; }
    } else if(equals(initialize)) {
        boundary="initialization_enter";
        unsigned short mission=0;
        accepted=mission_phase==MissionPhase::allocated && ReadMission(mission) && mission==allocation_mission &&
            initialization_scope.Enter(GetCurrentThreadId());
    } else if(equals(initialized)) {
        boundary="initialization_returned";
        accepted=mission_phase==MissionPhase::allocated && initialization_scope.Returned(GetCurrentThreadId());
    } else if(equals(start)) {
        boundary="runtime_enter";
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
            }
            else RestoreGameplayRules();
        }
    } else if(equals(terminate)) {
        boundary="termination_returned";
        if(OnTerminationThread()) {
            const bool owned_before=HasDynamiteSessionOwnership();
            ReconcileDynamiteSessionRelease(game_base,game_span,Log);
            if(owned_before && !HasDynamiteSessionOwnership()) ApplySessionReleased();
            cleanup_thread=GetCurrentThreadId();
            accepted=termination_scope.Returned(cleanup_thread);
            equipment_allocation_authorization.End();
            stock46_authorization.End(); stock46_before={};
            mgscoop::equipment_stock_writer::End();
            stock46_groups_authorized=stock46_groups_returned=false;
            mission_phase=MissionPhase::idle; allocation_thread=0;
            PartnerMissionEnd();
            if(world_on) mgscoop::world_link::LocalMissionEnd();
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
// on the runtime thread, LOG on the runtime or termination thread. A refused
// receipt stays literal (Lua treats it as inactive or not sent).
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
bool ObserveEntry(void*, const char* buffer, std::size_t size, const char* name,
                  mgscoop::script_bridge::Replacement& replacement) noexcept {
    AcquireSRWLockExclusive(&receipt_lock);
    struct Unlock { ~Unlock(){ ReleaseSRWLockExclusive(&receipt_lock); } } unlock;
    const auto count = InterlockedIncrement64(&entries);
    if(SelectOfflineEquipmentAllocationReceipt(buffer,size,name,replacement)) return true;
    if(SelectStock46PreloadReceipt(buffer,size,name,replacement)) return true;
    if(SelectEquipmentObservationReceipt(buffer,size,name,replacement)) return true;
    if(SelectActionEventsReceipt(buffer,size,name,replacement)) return true;
    if (SelectDebugReceipt(buffer, size, name, replacement)) return true;
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
        selected=mgscoop::SelectDeathPolicyPatch(buffer,size,name,replacement,host_create_enabled || client_enabled);
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
        const bool accepted=termination_scope.Enter(mission_phase==MissionPhase::running,mission_thread,
            GetCurrentThreadId(),HasDynamiteSessionOwnership(),TransferRuleThread);
        bool restored=false;
        if(accepted) {
            ClearUpperEndOwner(); mgscoop::partner_upper_probe::Clear("mission_terminating");
            equipment_allocation_authorization.End();
            stock46_authorization.End(); stock46_before={};
            mgscoop::equipment_stock_writer::End();
            stock46_groups_authorized=stock46_groups_returned=false;
            mission_terminating=true;
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
            Log("\r\nMGS Co-op script adapter 0.11.1; executable_fingerprint=accepted; install_after_DirectInput=true; earlier_loads_may_be_missed=true\r\n");
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
                    if (labc.net) {
                        ConfigureWorld(labc, directory);
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
                        net.interp_auto = labc.partner_interp == mgscoop::PartnerInterp::auto_;
                        net.interp_ms = labc.partner_interp_ms;
                        net.interp_min_ms = labc.partner_interp_min_ms;
                        net.interp_max_ms = labc.partner_interp_max_ms;
                        net.saves_redirected = lab::SavesRedirected();
                        net.directory = directory;
                        net.build = lab::ProbeVersion();
                        mgscoop::partner_net::Start(net);
                    } else {
                        if (labc.play_refused & mgscoop::kPlayRefusedGuestNoPeer) Log("link=disabled; reason=no_peer_addr\r\n");
                        Log("partner_net=disabled; reason=transport_bad_config\r\n");
                        if (labc.world) LogFormat("world=disabled; reason=net_off; t_ms=%llu\r\n", static_cast<unsigned long long>(QpcMicros() / 1000));
                    }
                } else if (labc.world) {
                    LogFormat("world=disabled; reason=partner_off; t_ms=%llu\r\n", static_cast<unsigned long long>(QpcMicros() / 1000));
                }
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
