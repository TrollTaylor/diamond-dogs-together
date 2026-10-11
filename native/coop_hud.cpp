// Original MGS Co-op in-game status line and toasts (model and shared state).
#include "mgscoop/coop_hud.hpp"

#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <thread>

#include "mgscoop/coop_reason_text.hpp"
#include "mgscoop/link_session.hpp"
#include "mgscoop/partner_slots.hpp"
#include "mgscoop/steam_net_abi.hpp"

namespace mgscoop::coop_hud {
namespace {
using link::LinkState;

// Portable, constant-initialized and trivially destructible: the critical
// sections are short copies (a view, one event, the telemetry).
class SpinLock {
public:
    void Lock() noexcept {
        while (held_.exchange(true, std::memory_order_acquire))
            while (held_.load(std::memory_order_relaxed)) std::this_thread::yield();
    }
    void Unlock() noexcept { held_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> held_{false};
};
class Guard {
public:
    explicit Guard(SpinLock& lock) noexcept : lock_(lock) { lock_.Lock(); }
    ~Guard() { lock_.Unlock(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    SpinLock& lock_;
};

std::atomic<int> g_mode{static_cast<int>(Mode::off)};
std::atomic<bool> g_enabled{false};

SpinLock g_view_lock;
LinkView g_view{};
SpinLock g_partner_lock;
PartnerView g_partner{};
SpinLock g_downed_lock;
DownedView g_downed{};

constexpr unsigned kEvents = 16;
SpinLock g_event_lock;
Event g_events[kEvents]{};
std::uint32_t g_event_sequence = 0;  // the newest event's sequence (0 = none yet)

SpinLock g_telemetry_lock;
Telemetry g_telemetry{};

template <std::size_t N>
void Copy(char (&dst)[N], const char* src) noexcept {
    std::size_t i = 0;
    for (; src && src[i] && i + 1 < N; ++i) {
        const auto c = static_cast<unsigned char>(src[i]);
        dst[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    dst[i] = 0;
}

template <typename... Args>
void Print(char* out, std::size_t capacity, const char* format, Args... args) noexcept {
    const int n = std::snprintf(out, capacity, format, args...);
    if (n < 0) out[0] = 0;  // a longer text is cut by snprintf (always NUL-terminated)
}

const char* RouteText(const LinkView& v) noexcept {
    if (!v.steam) return v.play ? "LAN/VPN" : "lab link";
    switch (static_cast<steam_net::Route>(v.route)) {
    case steam_net::Route::relay: return "Steam relay";
    case steam_net::Route::direct: return "Steam direct";
    default: return "Steam";
    }
}

std::uint64_t DownMs(const LinkView& v, std::uint64_t now_ms) noexcept {
    return v.down_ms + (now_ms >= v.publish_ms ? now_ms - v.publish_ms : 0);
}
}  // namespace

const char* ModeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::full: return "full";
    case Mode::off: return "off";
    default: return "compact";
    }
}

void Configure(Mode mode, bool enabled) noexcept {
    g_mode.store(static_cast<int>(mode));
    g_enabled.store(enabled);
    Guard guard(g_telemetry_lock);
    g_telemetry.enabled = enabled && mode != Mode::off;
    g_telemetry.mode = mode;
}

bool Enabled() noexcept { return g_enabled.load() && static_cast<Mode>(g_mode.load()) != Mode::off; }
Mode GetMode() noexcept { return static_cast<Mode>(g_mode.load()); }

Mode ModeFor(CoopHud lab_setting, CoopHud coop_setting, bool play) noexcept {
    const CoopHud chosen = lab_setting != CoopHud::unset ? lab_setting : play ? coop_setting : CoopHud::unset;
    switch (chosen) {
    case CoopHud::compact: return Mode::compact;
    case CoopHud::full: return Mode::full;
    case CoopHud::off: return Mode::off;
    default: return play ? Mode::compact : Mode::off;
    }
}


void PublishLink(const LinkView& view) noexcept {
    Guard guard(g_view_lock);
    g_view = view;
    g_view.valid = true;
}

bool GetLink(LinkView& out) noexcept {
    {
        Guard guard(g_view_lock);
        out = g_view;
    }
    return out.valid;
}

PartnerView MakePartnerView(const PartnerFacts& f) noexcept {
    PartnerView v{};
    v.valid = true; v.session = f.session; v.mission = f.mission; v.publish_ms = f.publish_ms;
    v.drive = !f.body_ready ? PartnerDrive::loading : f.native_selected && f.native_active ? PartnerDrive::native : PartnerDrive::clips;
    v.gun = !f.gun_enabled ? PartnerGun::off : PartnerGun::unavailable;
    if (!f.body_ready || !f.gun_enabled || !f.fresh_gun || !f.gun_fields_ok || f.gun_class != 1 || f.body_equip <= 0) return v;
    v.gun = PartnerGun::present;
    if (f.switching) v.gun = PartnerGun::switching;
    else if (f.remote_live && f.gate_current) {
        switch (static_cast<partner_slots::Gate>(f.gate)) {
        case partner_slots::Gate::match: v.gun = f.substitute ? PartnerGun::substitute : PartnerGun::match; break;
        case partner_slots::Gate::standin: v.gun = f.standin ? PartnerGun::standin : PartnerGun::present; break;
        case partner_slots::Gate::switching: v.gun = PartnerGun::switching; break;
        case partner_slots::Gate::mismatch: v.gun = PartnerGun::mismatch; break;
        case partner_slots::Gate::projectile: v.gun = PartnerGun::projectile; break;
        }
    }
    return v;
}
void PublishPartner(const PartnerView& view) noexcept { Guard guard(g_partner_lock); g_partner = view; }
bool GetPartner(PartnerView& out) noexcept { Guard guard(g_partner_lock); out = g_partner; return out.valid; }
PartnerView CurrentPartner(const PartnerView& v, std::uint32_t session, std::uint32_t mission, std::uint64_t now_ms) noexcept {
    if (v.valid && session == v.session && mission == v.mission && now_ms >= v.publish_ms && now_ms - v.publish_ms <= kPartnerFreshMs) return v;
    PartnerView stale{}; stale.drive = PartnerDrive::loading; stale.gun = PartnerGun::unavailable; return stale;
}
const char* PartnerDriveName(PartnerDrive d) noexcept {
    return d == PartnerDrive::native ? "native" : d == PartnerDrive::clips ? "clips" : "loading";
}
const char* PartnerGunName(PartnerGun g) noexcept {
    switch (g) {
    case PartnerGun::off: return "off";
    case PartnerGun::present: return "present";
    case PartnerGun::match: return "match";
    case PartnerGun::substitute: return "substitute";
    case PartnerGun::standin: return "standin";
    case PartnerGun::switching: return "switching";
    case PartnerGun::mismatch: return "mismatch";
    case PartnerGun::projectile: return "projectile";
    default: return "unavailable";
    }
}
const char* PartnerGunText(PartnerGun g) noexcept {
    if (g == PartnerGun::match) return "matched";
    if (g == PartnerGun::standin) return "stand-in";
    return PartnerGunName(g);
}
bool GunPresent(PartnerGun g) noexcept {
    switch (g) {
    case PartnerGun::present: case PartnerGun::match: case PartnerGun::substitute: case PartnerGun::standin:
    case PartnerGun::switching: case PartnerGun::mismatch: case PartnerGun::projectile: return true;
    default: return false;
    }
}
bool FormatPartnerJson(const PartnerView& v, std::uint32_t session, std::uint32_t mission, std::uint64_t now_ms,
                       char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const auto current = CurrentPartner(v, session, mission, now_ms);
    const int n = std::snprintf(out, capacity, "\"partner_view\":\"%s\",\"partner_gun\":\"%s\",\"partner_gun_present\":%u,\"partner_view_fresh\":%u,",
        PartnerDriveName(current.drive), PartnerGunName(current.gun), GunPresent(current.gun) ? 1u : 0u, current.valid ? 1u : 0u);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return false; }
    return true;
}

bool ParseDownedHud(const char* text, std::size_t size, DownedView& out) noexcept {
    out = {};
    constexpr char v1[] = "return 'MGS_COOP_DOWNED_HUD_V1' -- ";
    constexpr char v2[] = "return 'MGS_COOP_DOWNED_HUD_V2' -- ";
    constexpr auto length = sizeof(v1) - 1;
    if (!text || size <= length || size > 192) return false;
    const bool explicitCause = std::memcmp(text, v2, length) == 0;
    if (!explicitCause && std::memcmp(text, v1, length) != 0) return false;
    const auto count = explicitCause ? 8u : 6u;
    const char* cursor = text + length; const char* end = text + size;
    std::uint32_t v[8]{};
    for (unsigned i = 0; i < count; ++i) {
        const auto parsed = std::from_chars(cursor, end, v[i]);
        if (parsed.ec != std::errc() || parsed.ptr == cursor) return false;
        cursor = parsed.ptr;
        if (i + 1 != count) { if (cursor == end || *cursor != ' ') return false; ++cursor; }
    }
    if (cursor != end || v[0] > 99999 || v[1] > 65535 || v[2] > 1 || v[3] > 1 ||
        v[4] > 1 || v[5] > kReviveHoldMs || v[6] > 2 || v[7] > 2) return false;
    if (!explicitCause) { v[6] = v[2]; v[7] = v[3]; }
    if ((v[2] != 0) != (v[6] != 0) || (v[3] != 0) != (v[7] != 0) ||
        (v[4] && (!v[3] || v[2])) || (v[5] && (!v[4] || !v[3] || v[2]))) return false;
    if (!v[0]) {
        for (unsigned i = 1; i < count; ++i) if (v[i]) return false;
        return true;
    }
    if (!v[1] || (v[0] != 30010 && v[0] != 30020 && v[0] != 30050)) return false;
    out.valid = true; out.mission = v[0]; out.epoch = static_cast<std::uint16_t>(v[1]);
    out.localDown = v[2] != 0; out.teammateDown = v[3] != 0; out.inRange = v[4] != 0;
    out.progressMs = v[5]; out.localCause = static_cast<std::uint8_t>(v[6]); out.teammateCause = static_cast<std::uint8_t>(v[7]);
    return true;
}

void PublishDowned(const DownedView& view) noexcept { Guard guard(g_downed_lock); g_downed = view; }
bool GetDowned(DownedView& out) noexcept { Guard guard(g_downed_lock); out = g_downed; return out.valid; }
bool FormatDowned(const DownedView& v, std::uint32_t current_mission,
                  std::uint64_t now_ms, Line& out, float& progress) noexcept {
    out = {}; progress = 0;
    if (!v.valid || !v.epoch || v.mission != current_mission ||
        (v.mission != 30010 && v.mission != 30020 && v.mission != 30050) ||
        now_ms < v.publish_ms || now_ms - v.publish_ms > kDownedFreshMs || v.progressMs > kReviveHoldMs ||
        v.localCause > 2 || v.teammateCause > 2 || (!v.localDown && v.localCause) || (!v.teammateDown && v.teammateCause) ||
        (v.inRange && (!v.teammateDown || v.localDown)) ||
        (v.progressMs && (!v.inRange || !v.teammateDown || v.localDown))) return false;
    if (v.localDown) {
        Print(out.text, sizeof(out.text), v.localCause == 2 ?
              "Asleep - waiting to wake or for your teammate" : "Downed - waiting for your teammate to revive you");
        out.tone = v.localCause == 2 ? Tone::warn : Tone::bad; return true;
    }
    if (!v.teammateDown) return false;
    out.tone = Tone::warn;
    if (!v.inRange) Print(out.text, sizeof(out.text), v.teammateCause == 2 ?
        "Teammate asleep - get closer to wake them" : "Teammate down - get closer to revive");
    else {
        progress = static_cast<float>(v.progressMs) / static_cast<float>(kReviveHoldMs);
        Print(out.text, sizeof(out.text), v.teammateCause == 2 ? "Hold Action (E) to wake teammate - %u%%" :
              "Hold Action (E) to revive teammate - %u%%", v.progressMs * 100u / kReviveHoldMs);
    }
    return true;
}

void Post(EventKind kind, std::int32_t arg, const char* text) noexcept {
    Event e{};
    e.kind = kind;
    e.arg = arg;
    Copy(e.text, text);
    Guard guard(g_event_lock);
    e.sequence = ++g_event_sequence;
    g_events[e.sequence % kEvents] = e;
}

unsigned Take(Event* out, unsigned max, std::uint32_t& cursor) noexcept {
    if (!out || !max) return 0;
    unsigned n = 0;
    std::uint32_t newest = 0;
    {
        Guard guard(g_event_lock);
        newest = g_event_sequence;
        std::uint32_t from = cursor + 1;
        if (newest >= kEvents && from + kEvents <= newest) from = newest - kEvents + 1;  // overwritten ones are lost
        for (std::uint32_t s = from; s <= newest && n < max; ++s) out[n++] = g_events[s % kEvents];
    }
    if (n) cursor = out[n - 1].sequence;
    else if (newest < cursor) cursor = newest;  // ResetForTest
    return n;
}

bool FormatLine(const LinkView& v, const char* name, Mode mode, std::uint64_t now_ms, Line& out,
                const PartnerView* partner) noexcept {
    out = Line{};
    if (!v.valid || mode == Mode::off) return false;
    const auto state = static_cast<LinkState>(v.state);
    const bool named = name && name[0];
    const char* who = named ? name : v.peer[0] ? v.peer : (v.host ? "your friend" : "the host");
    const auto secs = static_cast<unsigned>(DownMs(v, now_ms) / 1000);
    char* t = out.text;
    const std::size_t n = sizeof(out.text);
    switch (state) {
    case LinkState::connected:
        if (partner) {
            const auto current = CurrentPartner(*partner, v.session, v.mission, now_ms);
            Print(t, n, "CO-OP [connected] %.24s - %s %u ms | %s; gun %s", who, RouteText(v), v.rtt_ms,
                  PartnerDriveName(current.drive), PartnerGunText(current.gun));
        } else Print(t, n, "CO-OP [connected] %s - %s %u ms", who, RouteText(v), v.rtt_ms);
        out.tone = Tone::good;
        break;
    case LinkState::listening:
        if (v.left) {
            Print(t, n, "CO-OP [%s left] waiting for them to come back", named ? name : "your friend");
            out.tone = Tone::warn;
        } else if (v.steam && !v.accepting) {
            Print(t, n, "CO-OP [not accepting joins] F9 > Partner / Co-op > Online to allow");
            out.tone = Tone::warn;
        } else if (v.steam) {
            Print(t, n, v.code_ready ? "CO-OP [waiting] code ready - F9 to copy/invite" : "CO-OP [waiting] starting Steam networking...");
            out.tone = Tone::info;
        } else {
            Print(t, n, "CO-OP [waiting for your friend]");
            out.tone = Tone::info;
        }
        break;
    case LinkState::lost:
        if (v.left) Print(t, n, "CO-OP [%s left] waiting for them to come back", named ? name : "your friend");
        else Print(t, n, "CO-OP [partner lost %u s] waiting for them to come back", secs);
        out.tone = Tone::warn;
        break;
    case LinkState::connecting:
    case LinkState::handshake:
        if (v.steam_end) Print(t, n, "CO-OP [joining %u s] %s", secs, coop_text::ShortSteamEnd(v.steam_end));
        else if (std::strcmp(v.reason, "bad_code") == 0 && !v.verified) Print(t, n, "CO-OP [joining %u s] host did not accept - wrong code?", secs);
        else Print(t, n, "CO-OP [joining %u s] %s", secs, who);
        out.tone = v.steam_end || v.reason[0] ? Tone::warn : Tone::info;
        break;
    case LinkState::reconnecting:
        if (v.left) Print(t, n, "CO-OP [the host left] waiting for them to come back (%u s)", secs);
        else Print(t, n, "CO-OP [reconnecting %u s]", secs);
        out.tone = Tone::warn;
        break;
    case LinkState::rejected:
        Print(t, n, "CO-OP [refused] %s", coop_text::ShortReason(v.reason));
        out.tone = Tone::bad;
        break;
    case LinkState::disabled:
        Print(t, n, "CO-OP [off] %s", v.reason[0] ? coop_text::ShortReason(v.reason) : "link unavailable");
        out.tone = Tone::bad;
        break;
    case LinkState::waiting:
        Print(t, n, "CO-OP [waiting for an invite] F9 > Partner / Co-op > Online to paste a code");
        out.tone = Tone::info;
        break;
    default:
        return false;
    }
    if (mode == Mode::full)
        Print(out.detail, sizeof(out.detail), "rtt %u ms | jitter %u ms | loss %u.%u%% | %s%s%s", v.rtt_ms, v.jitter_ms, v.loss_permille / 10,
              v.loss_permille % 10, v.own_build[0] ? v.own_build : "?", v.peer[0] ? " | " : "", v.peer);
    return true;
}

bool FormatToast(const Event& e, const LinkView& v, const char* name, char* out, std::size_t capacity, Tone& tone) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    const bool named = name && name[0];
    const char* who = named ? name : (v.host ? "your friend" : "the host");
    tone = Tone::info;
    switch (e.kind) {
    case EventKind::connected:
        Print(out, capacity, "Connected to %s (%s).", who, RouteText(v));
        tone = Tone::good;
        return true;
    case EventKind::reconnected:
        Print(out, capacity, "Reconnected to %s after %d s.", who, e.arg > 0 ? (e.arg + 500) / 1000 : 0);
        tone = Tone::good;
        return true;
    case EventKind::lost:
        if (v.host) Print(out, capacity, "Lost the connection to %s - waiting for them to come back.", who);
        else Print(out, capacity, "Lost the connection to the host - reconnecting...");
        tone = Tone::warn;
        return true;
    case EventKind::peer_left:
        if (v.host) Print(out, capacity, "%s left the session.", named ? name : "Your friend");
        else Print(out, capacity, "The host left - waiting for them to come back.");
        tone = Tone::warn;
        return true;
    case EventKind::refused:
        tone = Tone::bad;
        return coop_text::FormatLinkReason(e.text, v.remote_build, v.own_build, e.arg != 0, out, capacity) && out[0];
    case EventKind::version:
        tone = Tone::bad;
        return coop_text::FormatLinkReason("build_mismatch", e.text, v.own_build, true, out, capacity);
    case EventKind::steam_off:
        tone = Tone::bad;
        return coop_text::FormatLinkReason(e.text, "", v.own_build, true, out, capacity) && out[0];
    case EventKind::steam_end:
        tone = Tone::warn;
        return coop_text::FormatSteamEnd(e.arg, out, capacity) && out[0];
    case EventKind::invite_sent:
        Print(out, capacity, "Invite sent to %s.", e.text[0] ? e.text : "your friend");
        return true;
    case EventKind::joining:
        Print(out, capacity, e.arg ? "Joining %s from the invite..." : "Joining %s...", e.text[0] ? e.text : "the host");
        return true;
    case EventKind::join_refused:
        Print(out, capacity, "That join code is not valid (%s). Copy it again.", e.text[0] ? e.text : "?");
        tone = Tone::bad;
        return true;
    case EventKind::code_copied:
        Print(out, capacity, "Join code copied: paste it to your friend (it is a key, keep it private).");
        return true;
    case EventKind::no_body:
        tone = Tone::warn;
        return coop_text::FormatNoBody(e.text, out, capacity) && out[0];
    }
    return false;
}

void Model::Add(const char* text, Tone tone, std::uint64_t now_ms) noexcept {
    if (!text || !text[0]) return;
    for (auto& t : toasts_)
        if (t.until_ms > now_ms && std::strcmp(t.text, text) == 0) {
            t.until_ms = now_ms + kToastMs;
            return;
        }
    Toast* slot = nullptr;
    for (auto& t : toasts_)
        if (t.until_ms <= now_ms) { slot = &t; break; }
    if (!slot) {
        slot = &toasts_[next_ % kMaxToasts];
        ++next_;
    }
    Copy(slot->text, text);
    slot->tone = tone;
    slot->until_ms = now_ms + kToastMs;
}

void Model::OnEvent(const Event& event, const LinkView& view, const char* name, std::uint64_t now_ms) noexcept {
    char text[kToastChars]{};
    Tone tone = Tone::info;
    if (FormatToast(event, view, name, text, sizeof(text), tone)) Add(text, tone, now_ms);
}

unsigned Model::Active(std::uint64_t now_ms, const Toast** out, unsigned max) const noexcept {
    unsigned n = 0;
    // Newest first: the one expiring last.
    bool used[kMaxToasts]{};
    while (n < max && n < kMaxToasts) {
        int best = -1;
        for (unsigned i = 0; i < kMaxToasts; ++i)
            if (!used[i] && toasts_[i].until_ms > now_ms && (best < 0 || toasts_[i].until_ms > toasts_[best].until_ms)) best = static_cast<int>(i);
        if (best < 0) break;
        used[best] = true;
        out[n++] = &toasts_[best];
    }
    return n;
}

void NoteFrame(const Line* line, const char* newest_toast, bool hidden, std::uint32_t new_events, std::uint32_t new_toasts,
               std::uint64_t now_ms) noexcept {
    Guard guard(g_telemetry_lock);
    if (hidden) {
        ++g_telemetry.hidden;
    } else {
        ++g_telemetry.frames;
        Copy(g_telemetry.line, line ? line->text : "");
    }
    if (newest_toast && newest_toast[0]) Copy(g_telemetry.toast, newest_toast);
    g_telemetry.events += new_events;
    g_telemetry.toasts += new_toasts;
    g_telemetry.tick_ms = now_ms;
}

Telemetry GetTelemetry() noexcept {
    Guard guard(g_telemetry_lock);
    return g_telemetry;
}

namespace {
// JSON/status-safe copy: printable ASCII, '"' '\' ';' replaced.
template <std::size_t N>
void Safe(char (&dst)[N], const char* src, char replace_semicolon) noexcept {
    std::size_t i = 0;
    for (; src && src[i] && i + 1 < N; ++i) {
        const char c = src[i];
        dst[i] = (c == '"' || c == '\\') ? '\'' : (c == ';' && replace_semicolon) ? replace_semicolon : c;
    }
    dst[i] = 0;
}
}  // namespace

std::size_t FormatJson(const Telemetry& t, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    char line[kLineChars]{}, toast[kToastChars]{};
    Safe(line, t.line, 0);
    Safe(toast, t.toast, 0);
    const int n = std::snprintf(out, capacity,
                                ",\"hud\":{\"enabled\":%s,\"mode\":\"%s\",\"frames\":%u,\"hidden\":%u,\"events\":%u,\"toasts\":%u,"
                                "\"line\":\"%s\",\"toast\":\"%s\",\"tick_ms\":%llu}",
                                t.enabled ? "true" : "false", ModeName(t.mode), t.frames, t.hidden, t.events, t.toasts, line, toast,
                                static_cast<unsigned long long>(t.tick_ms));
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(n);
}

bool FormatStatus(const Telemetry& t, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    char line[64]{}, toast[48]{};
    Safe(line, t.line, ',');
    Safe(toast, t.toast, ',');
    const int n = std::snprintf(out, capacity, "hud;mode=%s;on=%u;frames=%u;line=%s;toast=%s", ModeName(t.mode), t.enabled ? 1u : 0u,
                                t.frames, line, toast);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return false;
    }
    return true;
}

void ResetForTest() noexcept {
    g_mode.store(static_cast<int>(Mode::off));
    g_enabled.store(false);
    {
        Guard guard(g_view_lock);
        g_view = LinkView{};
    }
    { Guard guard(g_partner_lock); g_partner = PartnerView{}; }
    { Guard guard(g_downed_lock); g_downed = DownedView{}; }
    {
        Guard guard(g_event_lock);
        for (auto& e : g_events) e = Event{};
        g_event_sequence = 0;
    }
    Guard guard(g_telemetry_lock);
    g_telemetry = Telemetry{};
}

}  // namespace mgscoop::coop_hud
