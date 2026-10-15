#pragma once
// Act census telemetry (docs/anim-round1-actions.md "Act census"): log only,
// no behavior change. Pure, portable and deterministic (mgs_coop_core): every
// observation and time is passed in; nothing allocates, throws or touches the
// game. partner_driver feeds one ActIn and one PadIn per tick and emits the
// formatted lines.
//
// 1. partner_act: an edge-triggered line on every change of the remote's act
//    (the interpolated pose sample's anim extension, Q+0x18 on the sender) and
//    of body 1's act (Q+0x18[1]), with the family names of the act catalogue
//    (action-anims.md section 3, corrected by critic 3.6: 3 crouch idle, 4
//    crouch move, 5 prone idle, 6 prone crawl, 57 dive, 58 Quiet's evade) and
//    the latency until body 1 showed the remote's act (match_ms), or "miss"
//    when the remote left the act (or held it kMatchTimeoutMs) unmatched.
//      partner_act; t=<tick_ms>; edge=remote|body|miss; remote=<act>(<name>);
//        body=<act>(<name>); match_ms=<ms>|pending|miss|-[; held_ms=<ms>]
//    Its own cap (kActLineCap per mission, then "partner_act=capped; limit=N")
//    and, while capped, a summary at most every kSummaryMs that something
//    happened in: partner_act=summary; remote_edges=..; body_edges=..;
//    matched=..; missed=..; dropped=..
// 2. partner_pad_dropped: per second, the remote's pad presses (its input
//    frames' edge sets: the sender's pad before this copy's mask) that body 1
//    did not get, by group, logged only when nonzero:
//      partner_pad_dropped; action=..; stock=..; call=..; subject=..; weapon=..;
//        cqc=..; light=..; vfire=..; marker=..; fire=..; other=..; other_bits=0x..;
//        mask=0x..; native=0|1
//    FIRE counts while the effective mask or the weapon gate strips it ("FIRE
//    outside the gate"); other: any other dropped bit outside the movement and
//    gun sets (DECIDE, MB_DEVICE, VEHICLE_CALL, SIDE_ROLL). Own cap kPadLineCap.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_act_log {

// The act family name (static). Uncertain families end in '?'; "?" = unknown.
const char* ActName(std::uint16_t act) noexcept;

inline constexpr std::uint32_t kActLineCap = 3000;
inline constexpr std::uint32_t kMatchTimeoutMs = 3000;
inline constexpr std::uint64_t kSummaryMs = 1000;

enum class Edge : std::uint8_t { remote, body, miss };
const char* EdgeName(Edge edge) noexcept;
// Line::match_ms special values (>= 0: the latency in ms).
inline constexpr std::int32_t kMatchPending = -1, kMatchMiss = -2, kMatchNone = -3;

struct Line {
    Edge edge;
    std::uint64_t t_ms;  // the log clock (GetTickCount64: both copies compare)
    bool remote_valid, body_valid;
    std::uint16_t remote, body;
    std::int32_t match_ms;
    std::uint32_t held_ms;  // miss: how long the remote held the missed act
};

struct ActIn {
    std::uint64_t now_ms;   // driver clock (latency)
    std::uint64_t tick_ms;  // log clock (t=)
    bool remote_valid;      // a live remote sample with a valid anim extension
    std::uint16_t remote_act;
    bool body_valid;        // body 1 realized and its anim fields read
    std::uint16_t body_act;
};

struct ActState {
    bool have_remote, have_body;
    std::uint16_t remote, body;
    std::uint64_t remote_since;
    bool pending;  // the current remote act was not matched yet
    std::uint32_t lines;
    bool capped;
    // Totals since Reset and the summary window (while capped).
    std::uint32_t remote_edges, body_edges, matched, missed, dropped;
    std::uint64_t window_ms;
    std::uint32_t w_remote, w_body, w_matched, w_missed, w_dropped;
};

struct ActOut {
    Line lines[3];
    unsigned count;   // lines to emit, in order (already within the cap)
    bool capped_now;  // emit "partner_act=capped; limit=<cap>" once
    bool summary;     // emit the summary of the window below
    std::uint32_t s_remote, s_body, s_matched, s_missed, s_dropped;
};

void Reset(ActState& state) noexcept;
ActOut Step(ActState& state, const ActIn& in, std::uint32_t cap = kActLineCap) noexcept;
// "partner_act; t=..; ...\r\n" (0 when it does not fit).
std::size_t FormatLine(const Line& line, char* out, std::size_t capacity) noexcept;
std::size_t FormatSummary(const ActOut& out, char* text, std::size_t capacity) noexcept;

// --- Dropped pad presses ----------------------------------------------------
enum Group : unsigned { kAction, kStock, kCall, kSubject, kWeapon, kCqc, kLight, kVehicleFire, kMarker, kFire, kOther, kGroups };
const char* GroupName(unsigned group) noexcept;
// The PlayerPad bits of each group (partner_pad_bits.hpp); kOther: computed.
std::uint32_t GroupBits(unsigned group) noexcept;
inline constexpr std::uint32_t kPadLineCap = 3600;
inline constexpr std::uint64_t kPadWindowMs = 1000;

struct PadIn {
    std::uint64_t now_ms;
    bool valid;             // the newest received input frame (partner_net LatestInput)
    std::uint32_t seq;      // its receive sequence (a new frame when it changes)
    std::uint8_t version;   // 1: no edge sets (presses = rising held bits)
    std::uint32_t held;     // the sender's held word
    std::uint32_t press;    // v2+: the frame's edge set press word (w50)
    std::uint8_t edge_seq;  // v2+: 0 none, else the edge set id (repeats carry the same id)
    // The bits body 1's window passes this tick (the effective native mask
    // minus the weapon gate); native: the native window is armed.
    std::uint32_t mask;
    bool native;
};
struct PadState {
    bool have;
    std::uint32_t seq, prev_held;
    std::uint8_t last_edge_seq;
    std::uint64_t window_ms;
    std::uint32_t counts[kGroups];
    std::uint32_t other_bits;
    std::uint32_t mask;
    bool native;
    std::uint32_t lines;
    bool capped;
    std::uint32_t presses;  // presses seen since Reset (statistics)
};
struct PadOut {
    bool line;        // emit the window below
    bool capped_now;  // emit "partner_pad_dropped=capped; limit=<cap>" once
    std::uint32_t counts[kGroups];
    std::uint32_t other_bits, mask;
    bool native;
};
void Reset(PadState& state) noexcept;
// The presses one new frame adds (0 for a repeat of the same frame or edge set).
std::uint32_t NewPresses(PadState& state, const PadIn& in) noexcept;
PadOut PadStep(PadState& state, const PadIn& in, std::uint32_t cap = kPadLineCap) noexcept;
std::size_t FormatPad(const PadOut& out, char* text, std::size_t capacity) noexcept;

} // namespace mgscoop::partner_act_log
