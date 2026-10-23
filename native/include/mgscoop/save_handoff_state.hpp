#pragma once
// Automatic save handoff (docs/save-handoff.md; join-fix phase 2 P2-14): the
// shared UI state between the handoff glue (save_handoff, the receive thread),
// the F9 Partner / Co-op > Shared save section (debug_menu_ui), the worker
// state.json (worker_debug), the world CFG reply r[12..13] (world_link) and
// link-status.json (partner_net PublishHandoffState). Copied data only;
// constant-initialized POD behind a seqlock/SRW pair; any thread; nothing
// allocates (steam_online_state.hpp style). mgs_coop_core.
//
// The enums below are the cross-track numbering: Token is the link-status
// "handoff" token order (P2-13), Toast the save_handoff toast codes the core
// emits and coop_text::FormatSaveHandoff renders (P2-15).
#include <cstddef>
#include <cstdint>

namespace mgscoop::save_handoff_state {

// link-status.json "handoff" tokens, in this order (P2-13). A missing key
// means off for tools.
enum class Token : std::uint8_t {
    off,                 // module not configured, or save_handoff=off
    idle,                // configured, nothing decided yet
    same,                // the sets match (or the scene matches): nothing to share
    wait_host,           // guest: needs the host's save; the host is not eligible yet
    offered,             // host: OFFER sent, waiting for WANT / DECLINE
    sending,             // host: CHUNKs going out (pct)
    partner_staged,      // host: the guest sent RESULT staged
    partner_restarting,  // host: the guest sent RESULT exiting
    receiving,           // guest: CHUNKs coming in (pct)
    staged,              // guest: verified stage committed (stage mode stops here)
    countdown,           // guest: request.json written; 10 s title countdown
    exiting,             // guest: RESULT exiting sent, WM_CLOSE posted
    kept,                // guest kept its own save (decline), or the host saw DECLINE user
    refused,             // the host's save cannot be shared (why = save_handoff_wire::Elig)
    failed,              // the transfer failed (why = save_handoff_wire::Fail)
    applied,             // guest: tooling applied a handoff before this process (applied.json)
};
inline constexpr std::uint8_t kTokenCount = 16;
// The P2-13 spelling ("off", "idle", ... "applied"); "off" for an unknown
// value. Unique ASCII names of at most 18 characters.
const char* TokenName(std::uint8_t token) noexcept;

// Toast codes (coop_hud EventKind::save_handoff arg; text "<a> <b>" in
// decimal). G = guest, H = host; the sentences are coop_text's
// (FormatSaveHandoff, spec user_visible_texts).
//   code  a                         b
//   G1    kb being received          0
//   G2    seconds left (10..1)       0
//   G3..G5, G8..G10                  0 0
//   G6    the host's Continue mission 0
//   G7    save_handoff_wire::Fail    0
//   G11   the host's mission         the settle seconds (online 2026-10-22 P3)
//   G12   the host's Continue mission 0 (P6)
//   H1..H5                           0 0 (H2: a = the mission the guest arrives in)
//   H6    save_handoff_wire::Fail    0
//   H7    the unsupported mission    0
//   H8    the settle seconds         0 (P3)
enum class Toast : std::uint8_t {
    g1_receiving = 1,
    g2_countdown = 2,
    g3_stage_only = 3,
    g4_wait_host = 4,
    g5_host_story_opening = 5,
    g6_host_unsupported = 6,
    g7_failed = 7,
    g8_kept = 8,
    g9_quit_manually = 9,
    g10_applied = 10,
    g11_host_settling = 11,  // the host is in game in 30010/30020/30050: its save is checked there, then copied
    g12_go_to_title = 12,    // the host's save is ready, but the transfer needs the guest's title screen
    h1_sending = 21,
    h2_partner_restarting = 22,
    h3_partner_elsewhere = 23,
    h4_story_opening = 24,
    h5_partner_kept = 25,
    h6_failed = 26,
    h7_unsupported = 27,
    h8_settling = 28,        // the own save is being bound by the in-game settle (then it is shared)
};

// View.why for the guest's wait_host token (online 2026-10-22 P3/P6): what the
// wait is for. The F9 Shared save line (coop_text::FormatSaveHandoffStatus)
// spells it out; 0 keeps the original "the host must reach Mother Base or free
// roam" sentence.
enum class WaitWhy : std::uint8_t {
    host_elsewhere = 0,  // the host's save is not known to continue into a co-op scene yet
    title = 1,           // the host's save is ready; the guest is in game (the transfer needs its title)
    settling = 2,        // the host is in game in 30010/30020/30050: the settle bind is under way
};

// CFG reply r[12] (P2-1): what Lua sees. pending = 1..4, busy = 2..4.
enum class LuaCode : std::uint8_t {
    none = 0,          // none, off or host role
    wait_host = 1,     // guest needs the host's save; host not eligible yet
    receiving = 2,
    staged = 3,        // staged or countdown
    exiting = 4,
    kept = 5,          // same, same_scene or declined
    failed = 6,        // failed or refused
};

struct View {
    bool available;            // the module is configured (F9 shows the Shared save section)
    std::uint8_t role;         // 0 none, 1 host, 2 guest (save_handoff_wire::Role)
    std::uint8_t token;        // Token
    std::uint8_t pct;          // 0..100 (receiving / sending)
    // Reason for the token: refused -> save_handoff_wire::Elig, failed ->
    // save_handoff_wire::Fail, kept -> save_handoff_wire::DeclineReason,
    // wait_host -> WaitWhy; else 0.
    std::uint8_t why;
    std::uint8_t lua_code;     // LuaCode (CFG r[12])
    std::uint32_t host_mission;  // the mission the host's save continues into (0 unknown)
    std::uint32_t bytes;       // the offered total (G1 / F9 "<kb> KB")
    bool can_keep;             // F9 'Keep my own save' enabled (wait_host, receiving, staged, countdown)
    bool travel_unlocked;      // Get() fills it from TravelUnlocked(); Publish ignores it
};
// Publish replaces the whole view; Get returns a consistent copy ({} apart
// from travel_unlocked before the first Publish, so the F9 lock and state.json
// work with save_handoff=off too).
void Publish(const View& view) noexcept;
View Get() noexcept;

// F9 'Keep my own save' (UI thread) -> the glue's on_idle (one slot; a request
// is taken exactly once).
void RequestKeepOwnSave() noexcept;
bool TakeKeepOwnSave() noexcept;

// F9 'Travel lock' checkbox: unlocked for the rest of the process. Any thread.
void SetTravelUnlocked(bool unlocked) noexcept;
bool TravelUnlocked() noexcept;

// Tests only.
void ResetForTest() noexcept;

}  // namespace mgscoop::save_handoff_state
