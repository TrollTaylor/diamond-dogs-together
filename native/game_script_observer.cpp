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
#include "mgscoop/soldier_patch.hpp"
#include "mgscoop/mission_termination.hpp"
#include "mgscoop/mission_initialization.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/lab_status.hpp"
#include "mgscoop/client_reset.hpp"

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
unsigned short allocation_mission = 0;
void* game_base = nullptr;
std::size_t game_span = 0;
// Lab observability (docs/lab-protocol.md). All of this is inert unless lab.ini
// is present and enabled; with no lab.ini the loader behaves exactly as before.
bool lab_present = false;
bool lab_active = false;                    // lab_present and the observer log is open
wchar_t coop_directory[32768]{};            // <exeDir>\mgs-coop
bool coop_directory_ready = false;
lab::Status lab_status{};
lab::Status lab_status_written{};
bool lab_status_written_valid = false;
lab::SessionStatus logged_session = lab::SessionStatus::none;
bool session_status_logged = false;
mgscoop::ClientResetMachine reset_machine{};
bool reset_key_released = false;            // Ctrl+F9 release latch (edge-triggered)
bool reset_refused_logged = false;
std::uint64_t armed_tick_ms = 0;            // when the guest became armed (for auto join)
bool auto_join_fired = false;
bool host_steamid_written = false;
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

// --- Lab observability helpers (all no-ops unless lab_active) -----------------

bool ReadMission(unsigned short& mission) noexcept;  // defined below

void BuildCoopPath(const wchar_t* name, wchar_t* out, std::size_t capacity) noexcept {
    out[0] = 0;
    if (coop_directory_ready)
        StringCchPrintfW(out, capacity, L"%s\\%s", coop_directory, name);
}
// Rewrites lab-status.json only when a field other than the timestamp changed.
void SyncLabStatus() noexcept {
    if (!lab_active || !coop_directory_ready) return;
    if (lab_status_written_valid && lab::SameContent(lab_status_written, lab_status)) return;
    const DWORD error = GetLastError();
    lab_status.updated_tick_ms = GetTickCount64();
    char json[lab::kStatusJsonCapacity]{};
    std::size_t length = 0;
    wchar_t path[32768]{};
    BuildCoopPath(L"lab-status.json", path, ARRAYSIZE(path));
    if (path[0] && lab::FormatStatusJson(lab_status, json, sizeof(json), length))
        lab::WriteFileAtomic(path, json, length);
    lab_status_written = lab_status;
    lab_status_written_valid = true;
    SetLastError(error);
}
unsigned MembersFor(lab::SessionStatus status) noexcept {
    switch (status) {
    case lab::SessionStatus::host_peer_present:
    case lab::SessionStatus::client_established: return 2;
    case lab::SessionStatus::host_ready:
    case lab::SessionStatus::client_requested: return 1;
    default: return 0;
    }
}
// Maps a ReadDynamiteSessionStatus literal to the protocol vocabulary, logs the
// session_status line on change, and refreshes lab-status.json.
void UpdateSessionStatus(const char* literal) noexcept {
    if (!lab_active) return;
    lab::SessionStatus mapped = logged_session;
    if (!lab::SessionStatusFromLiteral(literal, mapped)) return;  // keep previous
    if (!session_status_logged || mapped != logged_session) {
        char line[64]{};
        if (lab::FormatSessionStatusLine(mapped, line, sizeof(line))) Log(line);
        logged_session = mapped;
        session_status_logged = true;
    }
    unsigned short mission = 0;
    if (ReadMission(mission)) lab_status.mission = mission;
    lab_status.session = mapped;
    lab_status.members = MembersFor(mapped);
    SyncLabStatus();
}
void SetLabJoin(lab::JoinStatus join) noexcept {
    if (!lab_active) return;
    if (lab_status.join != join) { lab_status.join = join; SyncLabStatus(); }
}
void SetLabRefusal(const char* reason) noexcept {
    if (!lab_active) return;
    lab::SetRefusal(lab_status, reason);
    lab_status.join = lab::JoinStatus::refused;
    SyncLabStatus();
}
bool ConsumeTriggerFile(const wchar_t* name) noexcept {
    if (!coop_directory_ready) return false;
    const DWORD error = GetLastError();
    wchar_t path[32768]{};
    BuildCoopPath(name, path, ARRAYSIZE(path));
    bool consumed = false;
    if (path[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
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
void WriteHostSteamIdFile() noexcept {
    if (!lab_active || host_steamid_written || !coop_directory_ready) return;
    const std::uint64_t id = ReadLocalSteamId();
    if (!mgscoop::IsClientHostIdentity(id)) { Log("host_identity=unavailable; host_steamid_txt=0\r\n"); return; }
    char line[32]{};
    std::size_t length = 0;
    if (!lab::FormatSteamIdLine(id, line, sizeof(line), length)) return;
    wchar_t path[32768]{};
    BuildCoopPath(L"host-steamid.txt", path, ARRAYSIZE(path));
    if (path[0] && lab::WriteFileAtomic(path, line, length)) {
        host_steamid_written = true;
        Log("host_identity=written; host_steamid_txt=1\r\n");  // the id itself is never logged
    }
}

bool RestoreGameplayRules() noexcept {
    mgscoop::marker_loading::End();
    client_join_armed=false;
    client_key_released=false;
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
            allocation_thread=GetCurrentThreadId(); allocation_mission=mission;
            cleanup_thread=0;
            initialization_scope.Reset();
            mission_phase=MissionPhase::allocating; accepted=true;
        }
    } else if(equals(end)) {
        boundary="allocation_returned";
        if(OnAllocationThread()) { mission_phase=MissionPhase::allocated; accepted=true; }
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
            if(accepted) { mission_thread=GetCurrentThreadId(); mission_phase=MissionPhase::running; }
            else RestoreGameplayRules();
        }
    } else if(equals(terminate)) {
        boundary="termination_returned";
        if(OnTerminationThread()) {
            ReconcileDynamiteSessionRelease(game_base,game_span,Log);
            cleanup_thread=GetCurrentThreadId();
            accepted=termination_scope.Returned(cleanup_thread);
            mission_phase=MissionPhase::idle; allocation_thread=0;
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
bool ObserveEntry(void*, const char* buffer, std::size_t size, const char* name,
                  mgscoop::script_bridge::Replacement& replacement) noexcept {
    AcquireSRWLockExclusive(&receipt_lock);
    struct Unlock { ~Unlock(){ ReleaseSRWLockExclusive(&receipt_lock); } } unlock;
    const auto count = InterlockedIncrement64(&entries);
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
        const bool created=player_ready && !client_enabled && TryCreateHostSession(game_base,game_span,Log,heap_test_enabled,host_create_enabled);
        if(needs_player_patch && (!player_ready || (!client_enabled && !created))) RestoreGameplayRules();
        // Publish the host's own SteamID64 for the lab scripts (lab mode only;
        // the id value itself is never logged).
        if(created && host_create_enabled) WriteHostSteamIdFile();
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
                &mission,sizeof(mission),&read) && read==sizeof(mission) && mission==30050;
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
            mission_terminating=true;
            restored=RestoreGameplayRules();
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
                if(lab_active) {
                    const auto& labc=mgscoop::GetLabConfig();
                    const char* source=(labc.join_trigger && labc.auto_join_delay_s>0) ? "auto"
                                      : labc.join_trigger ? "trigger_file" : "keyboard";
                    char line[96]{};
                    StringCchPrintfA(line,sizeof(line),"client_join=armed; source=%s\r\n",source);
                    Log(line);
                    SetLabJoin(lab::JoinStatus::armed);
                }
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
                    if(labc.join_trigger && ConsumeTriggerFile(L"join.trigger")) trigger_key="trigger_file";
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
                    SetLabJoin(lab::JoinStatus::requested);
                    const bool accepted=TryCreateClientSession(game_base,game_span,client_host,Log,client_check_enabled);
                    if(!client_check_enabled) SetLabJoin(accepted ? lab::JoinStatus::requested : lab::JoinStatus::failed);
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
        const bool context=!mission_terminating && !gameplay_restore_pending && mgscoop::player_team::IsActive();
        mgscoop::ClientResetReason reason=mgscoop::ClientResetReason::none;
        if(lab_active && ConsumeTriggerFile(L"reset.trigger")) reason=mgscoop::ClientResetReason::trigger_file;
        else {
            // Ctrl+F9 edge, foreground-gated, available in any client mode.
            DWORD fg=0; const HWND f=GetForegroundWindow(); if(f) GetWindowThreadProcessId(f,&fg);
            if(fg!=GetCurrentProcessId()) reset_key_released=false;
            else if((GetAsyncKeyState(VK_F9)&0x8000)==0) reset_key_released=true;
            else if(reset_key_released && (GetAsyncKeyState(VK_CONTROL)&0x8000)!=0) {
                reset_key_released=false; reason=mgscoop::ClientResetReason::manual_key;
            } else reset_key_released=false;
        }
        const char* directive="noop";
        if(reason!=mgscoop::ClientResetReason::none && !reset_machine.active()) {
            const bool latched=client_join_consumed;
            // "owns" means a live client session still backs the borrowed target
            // (adapter ownership, or a recorded request whose session slots are
            // not yet empty). A consumed-but-sessionless latch (e.g. a refused
            // placeholder, or a synchronous refusal) is not owned and is safe to
            // re-arm. Closing a live session mid-mission is unverified.
            const bool owns=HasOwnedDynamiteClientSession() ||
                (HasRecordedClientRequest() && !RecordedClientRequestSlotsEmpty(game_base,game_span));
            const auto action=reset_machine.Request(reason,latched,owns);
            if(action==mgscoop::ClientResetAction::close_session) {
                // No guest has ever connected; closing a live client session
                // mid-mission is not statically verified safe, so refuse.
                reset_machine.Fail("owned_session_active");
                if(!reset_refused_logged) { Log("client_join=refused; reason=owned_session_active\r\n"); reset_refused_logged=true; }
                SetLabRefusal("owned_session_active");
                reset_machine.Clear();
            } else if(action==mgscoop::ClientResetAction::refuse) {
                reset_machine.Clear();  // nothing_to_reset: ignore quietly
            } else if(action==mgscoop::ClientResetAction::restore_rearm) {
                const auto rr=reset_machine.reason();
                if(RestoreClientRequestFields(game_base,game_span,Log)) {
                    ResetClientAttemptLatch();
                    client_join_consumed=false;
                    client_join_armed=context;  // re-arm immediately if the scene still accepts input
                    client_key_released=false;
                    armed_tick_ms=GetTickCount64();
                    auto_join_fired=false;
                    reset_refused_logged=false;
                    reset_machine.CompleteRestore();
                    lab_status.join_attempts+=1;
                    char line[160]{};
                    StringCchPrintfA(line,sizeof(line),"client_join=reset; reason=%s; join_attempts=%u\r\n",
                        mgscoop::ClientResetReasonName(rr),lab_status.join_attempts);
                    Log(line);
                    SetLabJoin(context ? lab::JoinStatus::armed : lab::JoinStatus::idle);
                    directive="rearm";
                } else {
                    reset_machine.Fail("restore_failed");
                    Log("client_join=refused; reason=restore_failed\r\n");
                    SetLabRefusal("restore_failed");
                    reset_machine.Clear();
                }
            }
        }
        // Only replace (and therefore log) when a reset actually re-armed.
        // Otherwise leave the literal unchanged so idle polling writes nothing.
        if(std::strcmp(directive,"rearm")==0) {
            static constexpr char rearm[]="return 'rearm'";
            replacement={rearm,sizeof(rearm)-1}; selected=true;
        }
    }
    constexpr char receipt[] = "return 'MGS_COOP_BOOTSTRAP_EXECUTED_V1'";
    constexpr char admission[]="return 'MGS_COOP_HOST_ADMISSION_V1'";
    constexpr char admission_observed[]="return 'MGS_COOP_HOST_ADMISSION_OBSERVED_V1'";
    constexpr char released[]="return 'MGS_COOP_HOST_RELEASE_V1'";
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
    const auto count = InterlockedIncrement64(&returns);
    if (Sample(count) || replaced) {
        char line[256]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "return_count=%lld thread=%lu tick_ms=%llu parser_result=%d replaced=%d entry_pairing=none\r\n",
            count, GetCurrentThreadId(), GetTickCount64(), result, replaced ? 1 : 0))) Log(line);
    }
}
}

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
    if (file != INVALID_HANDLE_VALUE) {
        if (ReadFile(file, mode, sizeof(mode), &read, nullptr)) {
            heap_test_enabled = read == 8 && std::memcmp(mode, "heap-v1\n", 8) == 0;
            client_check_enabled = read == 16 && std::memcmp(mode, "client-check-v1\n", 16) == 0;
            client_enabled = client_check_enabled || (read == 10 && std::memcmp(mode, "client-v1\n", 10) == 0);
            host_admission_enabled = read == 8 && std::memcmp(mode, "host-v3\n", 8) == 0;
            host_create_enabled = host_admission_enabled || (read == 8 && std::memcmp(mode, "host-v2\n", 8) == 0);
            host_enabled = client_enabled || host_create_enabled || heap_test_enabled || (read == 8 && std::memcmp(mode, "host-v1\n", 8) == 0);
            allocation_enabled = host_enabled || (read == 11 && std::memcmp(mode, "players-v1\n", 11) == 0);
            archive_enabled = allocation_enabled || (read == 11 && std::memcmp(mode, "archive-v1\n", 11) == 0);
            bootstrap_enabled = archive_enabled || (read == 13 && std::memcmp(mode, "bootstrap-v1\n", 13) == 0);
            enabled = bootstrap_enabled || (read == 11 && std::memcmp(mode, "observe-v1\n", 11) == 0);
        }
        CloseHandle(file);
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
            if(lab_present && SUCCEEDED(StringCchPrintfW(coop_directory,ARRAYSIZE(coop_directory),L"%s\\mgs-coop",directory))) {
                coop_directory_ready=true;
                lab_active=true;
                lab_status.pid=GetCurrentProcessId();
                lab_status.role=host_create_enabled ? lab::Role::host : client_enabled ? lab::Role::guest : lab::Role::none;
                lab_status.mode=host_admission_enabled ? lab::Mode::host_v3
                              : client_check_enabled ? lab::Mode::client_check_v1
                              : client_enabled ? lab::Mode::client_v1 : lab::Mode::off;
                lab_status.local_index=-1;
                SyncLabStatus();
            }
            Log("\r\nMGS Co-op script adapter 0.10.33; executable_fingerprint=accepted; install_after_DirectInput=true; earlier_loads_may_be_missed=true\r\n");
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
                archive_mounted = MountPlayerArchive(base, span, directory, Log);
                if (allocation_enabled) Log("mode=players_v1; mission_selection_trial=30050; live_actor_count=unverified\r\n");
                if (host_enabled) Log("mode=host_v1; one_attempt=1; peer_acceptance_requested=0\r\n");
                if (heap_test_enabled) Log("mode=heap_v1; paired_heap_trial=1 host_calls=0 allocator_hook=0\r\n");
                if (host_create_enabled) Log("mode=host_v2; dynamite_host_creation=enabled max_players=2 peer_acceptance_requested=0\r\n");
                if (host_admission_enabled) Log("mode=host_v3; peer_acceptance=after_mission_start; max_players=2\r\n");
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
            }
            // Keep the log and policy alive with the pinned DLL. No live unload.
        }
    }
    SetLastError(incoming_error);
}
