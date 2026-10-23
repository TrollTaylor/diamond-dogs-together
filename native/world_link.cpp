// Original MGS Co-op world sync glue: the core behind its leaf lock, the link
// hooks, the reliable pump, world.trigger and the receipt handlers
// (docs/world-sync-protocol.md).
#include "mgscoop/world_link.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/lab_status.hpp"
#include "mgscoop/log_budget.hpp"
#include "mgscoop/save_handoff.hpp"
#include "mgscoop/save_handoff_state.hpp"
#include "mgscoop/world_receipt.hpp"
#include "mgscoop/world_wire.hpp"

namespace mgscoop::world_link {
namespace {
namespace lab = mgscoop::lab;

// Constant-initialized, trivially destructible process state.
world::Core g_core;
SRWLOCK g_world_lock = SRWLOCK_INIT;
volatile LONG g_configured = 0;
volatile LONG g_pumping = 0;
LogFn g_log = nullptr;
bool g_lab_triggers = false;
bool g_placed_census = false;  // WS4 step 0: CFG r[14] (lab key partner_placed_census, lab only)
wchar_t g_trigger_path[lab::kMaxAtomicPath]{};
std::uint64_t g_trigger_poll_ms = 0;
// LOG receipts (0.13.6): a token bucket of kLogBurst lines refilled at kLogPerS
// lines/s, the old steady 200 per 10 s with room for a mission arrival's burst
// (the session of 2026-10-22 dropped 52 at the 30010 arrival: 108 resync applies
// and 77 phase lines in one 10 s window). world=log_cap counts the rest; it is
// logged before the next admitted line.
constexpr std::uint32_t kLogBurst = 600, kLogPerS = 20;
log_budget::Bucket g_log_bucket{};
thread_local char g_reply[world::reply_capacity]{};
std::uint8_t g_pump_buffer[world::kMaxReliableBundle];  // the single pumper only
// Under g_world_lock: the published pair-wait code (different missions settle
// for 15 s), the NOTE rate gate and the title progress of the last PROGRESS
// note (0 unknown, 1 ok, 2 story_start).
world::WaitPublisher g_waits;
world::NoteGate g_notes;
std::uint8_t g_progress = 0, g_published_wait = 1;
std::uint32_t g_title_mission = 0;

struct Locked {
    Locked() noexcept { AcquireSRWLockExclusive(&g_world_lock); }
    ~Locked() { ReleaseSRWLockExclusive(&g_world_lock); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
};

void Write(const char* line) noexcept {
    if (g_log) g_log(line);
}

// Writes the core's pending lines (outside the world lock).
void Flush(std::uint64_t now_ms) noexcept {
    for (unsigned guard = 0; guard < world::kLogLines; ++guard) {
        char text[world::kLogLineSize]{};
        bool taken = false;
        {
            Locked lock;
            taken = g_core.TakeLine(text, sizeof(text));
        }
        if (!taken) return;
        char line[world::kLogLineSize + 48]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "%s; t_ms=%llu\r\n", text, static_cast<unsigned long long>(now_ms))))
            Write(line);
    }
}

// Publishes the world view for link-status.json, the HUD and F9: the core is
// read and the settle rule applied under the world lock; partner_net (atomics
// only) and the one note-16 toast per different-missions pair come after it.
void PublishWorld(std::uint64_t now_ms) noexcept {
    partner_net::WorldView view{};
    std::uint32_t local = 0;
    bool post_mismatch = false;
    {
        Locked lock;
        view.level = g_core.level();
        view.paired = g_core.paired();
        view.partner_mission = g_core.remote_mission();
        local = g_core.local_mission();
        g_published_wait = g_waits.Update(g_core.pair_wait(), local, view.partner_mission, now_ms, post_mismatch);
        view.pair_wait = g_published_wait;
        view.progress = g_progress;
    }
    partner_net::PublishWorldState(view);
    if (post_mismatch) coop_hud::PostWorldNote(16, local, view.partner_mission, view.partner_mission, local);
}

// Sends queued R ops as bundles on reliable channel 1. One pumper at a time
// (the receive thread or the game thread); a busy pump is skipped (the
// receive thread pumps again within ~20 ms).
void Pump() noexcept {
    if (InterlockedCompareExchange(&g_pumping, 1, 0) != 0) return;
    for (unsigned bundles = 0; bundles < 8; ++bundles) {
        std::uint32_t generation = 0;
        std::size_t size = 0;
        {
            Locked lock;
            size = g_core.PeekBundle(g_pump_buffer, sizeof(g_pump_buffer), generation);
        }
        if (!size) break;
        const link::ReliableResult result = partner_net::SendReliable(1, g_pump_buffer, size, link::ReliableMode::append);
        if (result != link::ReliableResult::queued) break;
        Locked lock;
        g_core.CommitBundle(generation);
    }
    InterlockedExchange(&g_pumping, 0);
}

// world.trigger (lab only): at most once per second; read, delete, then apply.
void PollTrigger(std::uint32_t read_mission, bool terminating, std::uint64_t now_ms) noexcept {
    if (!g_lab_triggers || !g_trigger_path[0]) return;
    if (g_trigger_poll_ms && now_ms - g_trigger_poll_ms < 1000) return;
    g_trigger_poll_ms = now_ms;
    const DWORD error = GetLastError();
    HANDLE file = CreateFileW(g_trigger_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { SetLastError(error); return; }
    char text[world::max_trigger + 2]{};
    DWORD read = 0;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(file, &size) && ReadFile(file, text, sizeof(text) - 1, &read, nullptr);
    CloseHandle(file);
    DeleteFileW(g_trigger_path);
    SetLastError(error);
    world::Trigger trigger{};
    char canonical[world::max_trigger + 1]{};
    const char* reason = "token";
    const bool parsed = ok && size.QuadPart <= static_cast<LONGLONG>(world::max_trigger) &&
                        world::ParseTrigger(text, read, trigger, canonical, sizeof(canonical), reason);
    if (ok && size.QuadPart > static_cast<LONGLONG>(world::max_trigger)) reason = "too_long";
    {
        Locked lock;
        if (parsed) g_core.ApplyTrigger(trigger, canonical, read_mission, terminating, now_ms);
        else g_core.RejectTrigger(reason ? reason : "token");
    }
}

// ---- partner_net hooks (receive thread, no partner_net lock held).
void OnDatagramHook(const std::uint8_t* payload, std::size_t size) noexcept {
    const std::uint64_t now = partner_net::NowMs();
    {
        Locked lock;
        g_core.OnDatagram(payload, size, now);
    }
    Flush(now);
}

void OnSessionHook(std::uint32_t session) noexcept {
    const std::uint64_t now = partner_net::NowMs();
    {
        Locked lock;
        g_core.OnSession(session, now);
    }
    Flush(now);
}

void OnIdleHook(std::uint64_t now_ms) noexcept {
    std::uint8_t message[link::kMaxReliableMessage];
    for (unsigned drained = 0; drained < 32; ++drained) {
        std::size_t size = 0;
        if (!partner_net::DrainReliable(1, message, sizeof(message), size)) break;
        Locked lock;
        g_core.OnReliable(message, size, now_ms);
    }
    const partner_net::LinkInfo info = partner_net::GetLinkInfo();
    {
        Locked lock;
        g_core.OnLinkState(info.state == link::LinkState::connected, info.session, now_ms);
        g_core.Tick(now_ms);
    }
    Pump();
    Flush(now_ms);
    PublishWorld(now_ms);
}

const partner_net::WorldHooks kHooks{&OnDatagramHook, &OnIdleHook, &OnSessionHook};

const char* Reply(std::size_t length) noexcept { return length ? g_reply : nullptr; }
} // namespace

std::uint32_t ParseBuild(const char* version) noexcept {
    if (!version) return 0;
    std::uint32_t parts[3]{};
    unsigned part = 0, digits = 0;
    for (const char* p = version; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            if (++digits > 2) return 0;
            parts[part] = parts[part] * 10 + static_cast<std::uint32_t>(*p - '0');
        } else if (*p == '.' && digits && part < 2) {
            ++part;
            digits = 0;
        } else {
            break;  // a suffix ("-dev") ends the number
        }
    }
    if (part != 2 || !digits) return 0;
    return parts[0] * 10000 + parts[1] * 100 + parts[2];
}

void Configure(const Config& config) noexcept {
    if (InterlockedCompareExchange(&g_configured, 1, 0) != 0) return;
    g_log = config.log;
    g_lab_triggers = config.lab_triggers && !config.play;
    g_placed_census = config.placed_census && !config.play;
    if (g_lab_triggers && config.directory &&
        FAILED(StringCchPrintfW(g_trigger_path, ARRAYSIZE(g_trigger_path) - 4, L"%s\\mgs-coop\\world.trigger", config.directory)))
        g_trigger_path[0] = 0;
    world::Config core{};
    core.level = config.level;
    core.role = config.role;
    core.play = config.play;
    core.lab_triggers = g_lab_triggers;
    core.saves = config.saves;
    core.build = config.build;
    core.probe = config.play ? WorldProbe::none : config.probe;
    const std::uint64_t now = partner_net::NowMs();
    {
        Locked lock;
        g_core.Configure(core, now);
    }
    Flush(now);
    PublishWorld(now);
}

bool Configured() noexcept { return g_configured != 0; }
Status GetStatus() noexcept {
    Status s{};
    if (!Configured()) return s;
    Locked lock;
    s.configured = true;
    s.paired = g_core.paired();
    s.link_up = g_core.link_up();
    s.level = g_core.level();
    s.effective = g_core.eff();
    s.epoch = g_core.epoch();
    s.linked = g_core.linked();
    s.remote_level = g_core.remote_level();
    s.core_wait = g_core.pair_wait();
    s.pair_wait = g_published_wait;
    s.progress = g_progress;
    s.local_mission = g_core.local_mission();
    s.remote_mission = g_core.remote_mission();
    s.remote_location = g_core.remote_location();
    s.title_mission = g_title_mission;
    return s;
}

const partner_net::WorldHooks* Hooks() noexcept { return &kHooks; }

const char* HandleCfg(const char* source, std::size_t size, std::uint32_t read_mission, bool terminating) noexcept {
    if (!Configured()) return nullptr;
    std::uint32_t mission = 0;
    std::uint8_t phase = 0;
    if (!world::ParseCfg(source, size, mission, phase)) return nullptr;
    const std::uint64_t now = partner_net::NowMs();
    if (phase == 1) PollTrigger(read_mission, terminating, now);
    world::CfgReply reply{};
    {
        Locked lock;
        reply = g_core.Cfg(mission, phase == 1, read_mission, now);
    }
    // Save handoff (P2-1): r[12] the handoff code, r[13] the travel lock,
    // from the published atomics after the pure core (outside the world lock).
    reply.handoff = save_handoff::LuaCode(now);
    reply.lock = save_handoff::LockAllowed();
    // WS4 step 0: r[14] the attacker census (lab only, read-only logging).
    reply.census = g_placed_census ? 1 : 0;
    Flush(now);
    return Reply(world::FormatCfgReply(reply, g_reply, sizeof(g_reply)));
}

const char* HandleTx(const char* source, std::size_t size) noexcept {
    if (!Configured()) return nullptr;
    std::uint8_t code = 0;
    std::int64_t values[world::kMaxValues]{};
    unsigned count = 0;
    world::TxStatus status = world::ParseTx(source, size, code, values, count);
    const std::uint64_t now = partner_net::NowMs();
    static thread_local world::Datagram datagram;
    datagram.size = 0;
    if (status == world::TxStatus::ok) {
        Locked lock;
        status = g_core.Tx(code, values, count, now, &datagram);
    }
    // A U op goes out at once; not_connected is ignored (nothing on U is needed for correctness).
    if (status == world::TxStatus::ok && datagram.size) partner_net::SendDatagram(1, datagram.bytes, datagram.size);
    Pump();
    Flush(now);
    return Reply(world::FormatTxReply(status, g_reply, sizeof(g_reply)));
}

const char* HandleRx(const char* source, std::size_t size, std::uint32_t read_mission, bool terminating) noexcept {
    if (!Configured()) return nullptr;
    std::uint8_t mask = 0;
    if (!world::ParseRx(source, size, mask)) return nullptr;
    const std::uint64_t now = partner_net::NowMs();
    PollTrigger(read_mission, terminating, now);
    const std::uint32_t rtt = partner_net::GetLinkInfo().rtt_ms;
    world::RxReply reply{};
    {
        Locked lock;
        g_core.Rx(mask, rtt, now, reply);
    }
    Pump();
    Flush(now);
    return Reply(world::FormatRxReply(reply, g_reply, sizeof(g_reply)));
}

const char* HandleLog(const char* source, std::size_t size) noexcept {
    if (!Configured()) return nullptr;
    static constexpr char yes[] = "return true";
    const std::uint64_t now = partner_net::NowMs();
    char line[world::log_line_capacity]{};
    if (!world::FormatLogLine(source, size, now, line, sizeof(line))) return nullptr;
    const log_budget::Take take = log_budget::TakeToken(g_log_bucket, kLogBurst, kLogPerS, now);
    if (!take.emit) return yes;
    if (take.report_dropped) {
        char cap[96]{};
        if (SUCCEEDED(StringCchPrintfA(cap, ARRAYSIZE(cap), "world=log_cap; dropped=%u; t_ms=%llu\r\n", take.report_dropped,
                                       static_cast<unsigned long long>(now))))
            Write(cap);
    }
    Write(line);
    return yes;
}

const char* HandleNote(const char* source, std::size_t size) noexcept {
    if (!Configured()) return nullptr;
    std::uint8_t code = 0;
    std::uint32_t a = 0, b = 0;
    if (!world::ParseNote(source, size, code, a, b)) return nullptr;
    // Note 9 (CONTINUE_DURING_HANDOFF, P2-3 a=pct): Lua has no transfer percent
    // (CFG r[12] is only the code), so the native one is used.
    if (code == 9 && a == 0) a = save_handoff_state::Get().pct;
    static constexpr char yes[] = "return true";
    static constexpr char no[] = "return false";
    // 1 PROGRESS, 2 HELI_WAIT, 3 HELI_ALONE, 4 HELI_REFUSED; join-fix phase 2
    // (P2-3): 5 TRAVEL_LOCKED, 6 HOST_PARTNER_CANNOT_JOIN, 7 TITLE_CONTINUE_LOCKED,
    // 8 TRAVEL_ALONE, 9 CONTINUE_DURING_HANDOFF, 10 HANDOFF_KEPT_OWN,
    // 11 GUEST_LANDING_ELSEWHERE, 12 SORTIE_HELD; 13..15 have no meaning yet.
    const bool known = code >= 1 && code <= 12;
    const std::uint64_t now = partner_net::NowMs();
    std::uint32_t partner = 0, local = 0;
    bool post = false, log = false;
    {
        Locked lock;
        partner = g_core.remote_mission();
        local = g_core.local_mission();
        g_notes.Accept(code, a, b, now, post, log);
        if (code == 1) {
            g_progress = b ? 2 : 1;
            g_title_mission = a;
        }
    }
    if (log) {
        char line[160]{};
        const HRESULT formatted =
            known ? StringCchPrintfA(line, ARRAYSIZE(line), "world=note; code=%u; a=%u; b=%u; partner=%u; t_ms=%llu\r\n", unsigned(code),
                                     a, b, partner, static_cast<unsigned long long>(now))
                  : StringCchPrintfA(line, ARRAYSIZE(line), "world=note; code=%u; result=unknown; t_ms=%llu\r\n", unsigned(code),
                                     static_cast<unsigned long long>(now));
        if (SUCCEEDED(formatted)) Write(line);
    }
    // Code 1 toasts only a story start, and not while a save handoff runs for
    // this guest (the handoff toasts replace it; the log line stays).
    const bool story_toast = code != 1 || (b == 1 && !save_handoff::GuestHandoffActive());
    if (post && known && story_toast) coop_hud::PostWorldNote(code, a, b, partner, local);
    PublishWorld(now);
    return known ? yes : no;
}

WarpGrant FindGrant(std::uint16_t seq) noexcept {
    WarpGrant out{};
    if (!Configured()) return out;
    const std::uint64_t now = partner_net::NowMs();
    world::Grant grant{};
    {
        Locked lock;
        if (!g_core.FindGrant(seq, now, grant)) return out;
    }
    out.found = true;
    out.seq = grant.seq;
    out.epoch = grant.epoch;
    out.kind = grant.kind;
    out.flags = grant.flags;
    out.ent = grant.ent;
    for (unsigned i = 0; i < 3; ++i) out.pos[i] = static_cast<float>(grant.pos_mm[i]) * 0.001f;
    out.yaw = static_cast<float>(grant.yaw_mrad) * 0.001f;
    out.age_ms = now - grant.rx_ms;
    out.serial = grant.serial;
    return out;
}

void MissionRuntime(std::uint32_t) noexcept {
    // The CFG start receipt carries the mission state; nothing else to do yet
    // (extension point for W3 mission following).
}

void LocalMissionEnd() noexcept {
    if (!Configured()) return;
    const std::uint64_t now = partner_net::NowMs();
    {
        Locked lock;
        g_core.LocalMissionEnd(now);
    }
    Flush(now);
}

} // namespace mgscoop::world_link
