#pragma once
// The in-game co-op status line and toasts (docs/two-pc-play.md "In game").
//
// Producers post copied data only: partner_net's transport thread publishes a
// LinkView (state, route, round trip, reason, the partner's goodbye, the
// Steam end reason) and posts events (connected, lost, reconnected, partner
// left, refused, version mismatch, Steam unavailable); the Steam owner pass
// posts invite_sent, the script observer no_body (a partner mission started
// on a pack list the player package selection refused, docs/two-pc-play.md
// "In game") and the F9 panel code_copied (the HUD is hidden while the
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
    std::uint32_t session, mission; // refuse views from a previous link/mission
};
void PublishLink(const LinkView& view) noexcept;
bool GetLink(LinkView& out) noexcept;

// ---- Copied partner status: natural owner publishes even with F9 off.
// Enum-only facts; no game reads, historical gun presence or writer receipts.
enum class PartnerDrive : std::uint8_t { loading, clips, native };
enum class PartnerGun : std::uint8_t { off, unavailable, present, match, substitute, standin, switching, mismatch, projectile };
struct PartnerView {
    bool valid;
    PartnerDrive drive;
    PartnerGun gun;
    std::uint32_t session, mission;
    std::uint64_t publish_ms;
};
struct PartnerFacts {
    std::uint32_t session, mission;
    std::uint64_t publish_ms;
    bool body_ready, native_selected, native_active, gun_enabled;
    bool fresh_gun, gun_fields_ok, remote_live, gate_current;
    std::int32_t gun_class, body_equip;
    bool switching, substitute, standin;
    std::uint8_t gate; // partner_slots::Gate, only when fresh/current
};
inline constexpr std::uint64_t kPartnerFreshMs = 1000;
PartnerView MakePartnerView(const PartnerFacts& facts) noexcept;
void PublishPartner(const PartnerView& view) noexcept;
bool GetPartner(PartnerView& out) noexcept;
// Stale, future-clock, other session/mission: loading and unavailable.
PartnerView CurrentPartner(const PartnerView& view, std::uint32_t session, std::uint32_t mission, std::uint64_t now_ms) noexcept;
const char* PartnerDriveName(PartnerDrive drive) noexcept;
const char* PartnerGunName(PartnerGun gun) noexcept;
const char* PartnerGunText(PartnerGun gun) noexcept;
bool GunPresent(PartnerGun gun) noexcept;
// Additive link-status keys with trailing comma. False/empty on overflow.
bool FormatPartnerJson(const PartnerView& view, std::uint32_t session, std::uint32_t mission, std::uint64_t now_ms,
                       char* out, std::size_t capacity) noexcept;

// ---- Copied local incapacitation/revive facts from the natural Lua owner.
// Publishing is independent of the HUD/menu toggle. A stopped or unpaired
// world publishes an invalid empty view; the draw thread reads no game data.
struct DownedView {
    bool valid;
    std::uint32_t mission;
    std::uint16_t epoch;
    std::uint64_t publish_ms;
    bool localDown, teammateDown, inRange;
    // Geometric 3 m approach. V2 receipts copy this from inRange. V3 can be
    // close while the Action hold is not yet observed, so the prompt does not
    // say "get closer" for a teammate who is already beside you.
    // Not named near: windows.h defines that as a macro.
    bool closeEnough;
    std::uint32_t progressMs;
    std::uint8_t localCause, teammateCause; // 0 healthy/legacy unknown, 1 wounded, 2 asleep
    // Stock player carry (coop_carry): 0 none, 1 the local player carries the
    // teammate, 2 the local player is being carried. Set natively, not by Lua.
    std::uint8_t carry;
};
inline constexpr std::uint64_t kDownedFreshMs = 500;
inline constexpr std::uint32_t kReviveHoldMs = 3000;
// Parse only a bounded V1/V2 copied receipt. No game reads or publication.
// V1 down flags infer wounded; V2 requires a matching explicit cause.
bool ParseDownedHud(const char* text, std::size_t size, DownedView& out) noexcept;
void PublishDowned(const DownedView& view) noexcept;
bool GetDowned(DownedView& out) noexcept;

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
    code_copied,
    no_body        // arg: the mission, text: the refused pack's scene (partner::PackSceneName)
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
    // Rescue prompt only: key hints "K\tlabel" separated by '\n' (drawn as key caps).
    char keys[kLineChars];
};
// False and empty for stale/future/other-mission/unsupported/invalid facts,
// healthy players, or progress that cannot belong to an in-range live reviver.
// progress is 0..1; only teammateDown && inRange && !localDown draws a bar.
// A healthy player near a downed teammate gets the two stock actions as key
// hints: hold E (wake/revive, the co-op hold) and hold R (the stock carry).
bool FormatDowned(const DownedView& view, std::uint32_t current_mission,
                  std::uint64_t now_ms, Line& out, float& progress) noexcept;
// The status line for `view` (`name`: the partner's display name, may be "").
// False when nothing is shown (no view, link off).
bool FormatLine(const LinkView& view, const char* name, Mode mode, std::uint64_t now_ms, Line& out,
                const PartnerView* partner = nullptr) noexcept;
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
