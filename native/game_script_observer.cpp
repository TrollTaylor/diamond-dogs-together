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
#include "mgscoop/lab_config.hpp"
#include "mgscoop/lab_status.hpp"
#include "mgscoop/client_reset.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/partner_driver.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_dm_patch.hpp"
#include "mgscoop/crash_probe.hpp"
#include "mgscoop/early_patches.hpp"
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
    PartnerApplyPosition, PartnerReadVelocity, PartnerLatestInput, PartnerInstallWrap, PartnerInterpMs};
// The mission of the current partner epoch (link-status.json "mission").
std::uint32_t partner_mission = 0;
// Crash-probe context flags: bit0 input window armed, bit1 DM patch applied.
std::uint32_t PartnerCtxFlags() noexcept {
    return (partner_driver.mission.input_armed ? 1u : 0u) | (partner_driver.mission.dm_patched ? 2u : 0u);
}

void PartnerMissionStart(std::uint32_t mission) noexcept {
    if (!partner_on) return;
    pd::MissionStart(partner_driver, partner_hooks);
    mgscoop::partner_net::ResetRemote();
    mgscoop::partner_net::SetEpoch(mission);
    partner_mission = mission;
    mgscoop::partner_net::PublishPartnerState(false, false, mission);
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, 0, PartnerCtxFlags());
}
void PartnerMissionEnd() noexcept {
    if (!partner_on) return;
    pd::MissionEnd(partner_driver, partner_hooks);
    mgscoop::partner_net::ResetRemote();
    mgscoop::partner_net::SetEpoch(0);
    partner_mission = 0;
    mgscoop::partner_net::PublishPartnerState(false, false, 0);
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, 0, PartnerCtxFlags());
}

const char* HandlePartnerCfg() noexcept {
    static constexpr char yes[]="return true", no[]="return false";
    unsigned short mission = 0;
    const char* precheck = nullptr;
    if (!OnRuntimeThread() || mission_terminating) precheck = "not_runtime_thread";
    else if (!ReadMission(mission) || mission != 30050) precheck = "mission";
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
    pd::Stop(partner_driver, partner_hooks, mgscoop::GetLabConfig().partner_teardown, mission_terminating);
    mgscoop::partner_net::ResetRemote();
    mgscoop::crash_probe::SetContext(0, partner_driver.mission.realized ? 1u : 0u, PartnerCtxFlags());
    return yes;
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
    partner::Reply reply{};
    const pd::Clock clock{mgscoop::partner_net::NowMs(), GetTickCount64()};
    if (!pd::Tick(partner_driver, partner_hooks, mgscoop::GetLabConfig(), buffer, size, clock, mission_terminating, reply))
        return nullptr;
    if (!partner::FormatReply(reply, partner_reply, sizeof(partner_reply))) return nullptr;
    mgscoop::crash_probe::SetContext(partner_driver.settings.stage, partner_driver.mission.realized ? 1u : 0u, PartnerCtxFlags());
    mgscoop::partner_net::PublishPartnerState(partner_driver.mission.realized, partner_driver.mission.parts_loaded, partner_mission);
    const std::uint64_t ended_us = QpcMicros();
    mgscoop::partner_net::RecordNativeMicros(Clamp32(ended_us >= started_us ? ended_us - started_us : 0));
    return partner_reply;
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
            if(accepted) { mission_thread=GetCurrentThreadId(); mission_phase=MissionPhase::running; PartnerMissionStart(allocation_mission); }
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
            mission_phase=MissionPhase::idle; allocation_thread=0;
            PartnerMissionEnd();
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
bool ObserveEntry(void*, const char* buffer, std::size_t size, const char* name,
                  mgscoop::script_bridge::Replacement& replacement) noexcept {
    AcquireSRWLockExclusive(&receipt_lock);
    struct Unlock { ~Unlock(){ ReleaseSRWLockExclusive(&receipt_lock); } } unlock;
    const auto count = InterlockedIncrement64(&entries);
    if(partner_on) {
        const auto partner_kind=partner::Classify(buffer,size,name);
        if(partner_kind!=partner::Receipt::none) return SelectPartnerReceipt(partner_kind,buffer,size,name,count,replacement);
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
    if (pd::ConsumeReturn(partner_quiet)) { InterlockedIncrement64(&returns); return; }
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
            Log("\r\nMGS Co-op script adapter 0.11.0; executable_fingerprint=accepted; install_after_DirectInput=true; earlier_loads_may_be_missed=true\r\n");
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
                    }
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
