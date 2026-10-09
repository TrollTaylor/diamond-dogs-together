#pragma once
// The in-game co-op status line and toasts (docs/two-pc-play.md "In game").
//
// Producers post copied data only: partner_net's transport thread publishes a
// LinkView (state, route, round trip, reason, the partner's goodbye, the
// Steam end reason) and posts events (connected, lost, reconnected, partner
// left, refused, version mismatch, Steam unavailable); the Steam owner pass
// posts invite_sent and the F9 panel code_copied (the HUD is hidden while the
// menu is open, so those toasts show for what is left of their 4 s after it
// closes). The overlay's Present thread
// owns one Model, turns events into 4 s toasts and draws the line top-right
// (compact: one line; full: plus a detail line) while the F9 menu is closed.
// Nothing here reads game memory or calls Steam; the sentences are
// coop_text's. Constant-initialized state; nothing allocates.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::coop_hud {

enum class Mode : std::uint8_t { compact, full, off };
const char* ModeName(Mode mode) noexcept;
// partner_net Start: the HUD exists only with a link (net=1, link v2).
void Configure(Mode mode, bool enabled) noexcept;
bool Enabled() noexcept;  // configured, enabled and not off
Mode GetMode() noexcept;
// lab.ini coop_hud wins; then coop.ini hud (play only); then compact in play
// and off in the one-PC lab (its default stays unchanged).
Mode ModeFor(CoopHud lab_setting, CoopHud coop_setting, bool play) noexcept;

// ---- The link as the status line needs it (link::LinkState values).
struct LinkView {
    bool valid;
    std::uint8_t state;        // link::LinkState
    bool host, steam, play;
    bool accepting, code_ready, waiting;
    bool left;                 // the partner sent a CLOSE (cleared by the next session)
    bool verified;             // the reject was authenticated
    std::uint8_t route;        // steam_net::Route (0 none for UDP)
    std::uint32_t rtt_ms, jitter_ms, loss_permille;
    std::uint32_t down_ms;     // since the link last left connected (0 while connected)
    std::int32_t steam_end;    // the guest's last 1252 end reason, 0 none
    char reason[32];           // link-status "reason" token
    char peer[24];             // masked ("steam:...1234" / "a.b.x.x:port")
    char remote_build[24];
    char own_build[24];
    std::uint64_t publish_ms;  // GetTickCount64 at publish
};
void PublishLink(const LinkView& view) noexcept;
bool GetLink(LinkView& out) noexcept;

// ---- Events (any thread; a 16-entry ring, the oldest dropped when full).
enum class EventKind : std::uint8_t {
    connected,     // arg: session
    reconnected,   // arg: gap ms
    lost,          // arg: after ms
    peer_left,
    refused,       // text: reason token, arg: verified
    version,       // text: the remote build
    steam_off,     // text: the steam_<reason> token
    steam_end,     // arg: ESteamNetConnectionEnd
    invite_sent,   // text: the friend's name (or masked id)
    joining,       // text: the masked host, arg: 1 from an invite
    join_refused,  // text: the parser status
    code_copied
};
struct Event {
    EventKind kind;
    std::int32_t arg;
    char text[48];
    std::uint32_t sequence;
};
void Post(EventKind kind, std::int32_t arg, const char* text) noexcept;
// Copies the events newer than `cursor` (oldest first) and advances it.
unsigned Take(Event* out, unsigned max, std::uint32_t& cursor) noexcept;

// ---- The Present thread's model (pure).
inline constexpr std::uint64_t kToastMs = 4000;
inline constexpr unsigned kMaxToasts = 3;
inline constexpr std::size_t kLineChars = 112;
inline constexpr std::size_t kToastChars = 200;
enum class Tone : std::uint8_t { info, good, warn, bad };
struct Toast {
    char text[kToastChars];
    std::uint64_t until_ms;
    Tone tone;
};
struct Line {
    char text[kLineChars];
    char detail[kLineChars];  // full mode only ("" otherwise)
    Tone tone;
};
// The status line for `view` (`name`: the partner's display name, may be "").
// False when nothing is shown (no view, link off).
bool FormatLine(const LinkView& view, const char* name, Mode mode, std::uint64_t now_ms, Line& out) noexcept;
// The toast sentence for one event (false: no toast for it).
bool FormatToast(const Event& event, const LinkView& view, const char* name, char* out, std::size_t capacity, Tone& tone) noexcept;
class Model {
public:
    // A new toast (an identical active one is extended instead); the oldest
    // is replaced when kMaxToasts are active.
    void Add(const char* text, Tone tone, std::uint64_t now_ms) noexcept;
    void OnEvent(const Event& event, const LinkView& view, const char* name, std::uint64_t now_ms) noexcept;
    // Active toasts, newest first; returns the count (<= kMaxToasts).
    unsigned Active(std::uint64_t now_ms, const Toast** out, unsigned max) const noexcept;
    std::uint32_t& cursor() noexcept { return cursor_; }

private:
    Toast toasts_[kMaxToasts]{};
    unsigned next_ = 0;
    std::uint32_t cursor_ = 0;
};

// ---- Telemetry (Present thread writes; state.json "hud", hud_status).
struct Telemetry {
    bool enabled;
    Mode mode;
    std::uint32_t frames;        // Present frames that drew the HUD
    std::uint32_t hidden;        // frames hidden (demo, menu open)
    std::uint32_t toasts;        // toasts created
    std::uint32_t events;        // events taken
    char line[kLineChars];
    char toast[kToastChars];     // the newest toast text
    std::uint64_t tick_ms;       // last draw (GetTickCount64)
};
void NoteFrame(const Line* line, const char* newest_toast, bool hidden, std::uint32_t new_events, std::uint32_t new_toasts,
               std::uint64_t now_ms) noexcept;
Telemetry GetTelemetry() noexcept;
// `,"hud":{...}` (a leading comma) for the worker state.json; 0 when it does not fit.
std::size_t FormatJson(const Telemetry& t, char* out, std::size_t capacity) noexcept;
// "hud;mode=compact;on=1;frames=N;line=<line>;toast=<newest>" (<= 160 chars,
// the texts cut to fit; ';' replaced by ',').
bool FormatStatus(const Telemetry& t, char* out, std::size_t capacity) noexcept;

// Tests only.
void ResetForTest() noexcept;

}  // namespace mgscoop::coop_hud
