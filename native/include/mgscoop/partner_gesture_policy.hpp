#pragma once
// Gestures for the native puppet (docs/native-puppet.md "Gestures"), pure part:
// the upper-act edge sender, the companion wire, the node tables and the
// decisions. No game access (mgs_coop_core); partner_gesture.cpp runs the
// native side and partner_driver.cpp wires both.
//
// A pickup, a hand sign or a wall knock is an upper-body action: the lower act
// Q+0x18 does not change, so nothing on the existing streams tells the peer.
// The sender samples its own upper act byte Q+0x20[local] every driver tick and
// sends each rising edge into one of the gesture values below as a 12-byte
// message on reliable channel 2 (first byte 'G'). An older receiver has no
// classifier for 'G' and drops it through its equipment-manifest decoder
// (Result::magic, a counter only): the manifest, ride, carry and capability
// messages on the same channel are unaffected, and partner_action_events v1 is
// never touched (its Valid would reject the whole frame for a new kind).
//
// Stock references (mgsvtpp.exe 1.0.15.4, RVAs; re-traverse-gesture.md section 5):
// - Q+0x20[id] = 0x15 PickUpActionPluginImpl Wake (0x120FD9A, cleared in Sleep
//   0x120EB37); 0x17 KnockActionPluginImpl::StateKnock (0x11E7210, event 2);
//   0x18 StateHandSignGo (0x11E6A40); 0x19 StateHandSignStop (0x11E6C30).
// - Pickup (FUN_14120E520): vt+0x170 SetCurrentNode(cs, id, 2, node, 0.5f, 0);
//   refused while Q+0x18 == 0x4D (box) or Q+0x28 != 0.
// - Hand signs and the knock: vt+0x160 ReserveToResetNode(cs, id, 2, node, 1.0f, 0).
// - Hand sign end (event 1): Q+0x38[id] > 1 or cs vt+0x2B8(id, 3); the knock
//   ends on vt+0x2B8(id, 3); the pickup on its motion end (vt+0x228(id, 2)).
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_gesture_policy {

enum class Kind : std::uint8_t { none = 0, pickup = 1, sign_go = 2, sign_stop = 3, knock = 4 };
inline constexpr std::uint8_t kKindMax = 4;
inline constexpr std::uint8_t kUpperPickup = 0x15, kUpperKnock = 0x17, kUpperSignGo = 0x18, kUpperSignStop = 0x19;
// The gesture an upper act byte stands for (none for every other value: 0
// idle, 5 reload, 0x16 / 0x1A..0x1B the other plugins).
Kind KindForUpper(std::uint8_t upper) noexcept;
const char* KindName(Kind kind) noexcept;

// How a node is requested on graph 2 (the upper channel).
enum class Method : std::uint8_t { none, set_current, reserve_reset };  // cs vt+0x170 / vt+0x160
const char* MethodName(Method method) noexcept;
inline constexpr std::uint32_t kGraphUpper = 2;   // the upper graph
inline constexpr std::uint32_t kEndLayer = 3;     // vt+0x2B8 blend layer of the hand-sign end test
inline constexpr std::uint64_t kStateNull = 0xC1A2050D3802ULL;  // the validated return (partner_upper_manual_idle kIdle)
inline constexpr float kPickupBlend = 0.5f;       // DAT_1420A72C4
inline constexpr float kSignBlend = 1.0f;         // DAT_1420A72CC
inline constexpr float kNullBlend = 0.0f;

// The stock node for a gesture. stance: the remote's Q+0x30 (1 stand, 2 squat,
// 3 crawl; anything else is the stock "other" row); moving: its Q+0x38 != 0;
// hand: body 1's Q+0x58 != 0 (the knock's arm variant). The item height and the
// per-id Left/Right flag (PS+0x2F0) are not on the wire: the low item and flag 0
// rows are used. A pickup outside stand/squat is refused like stock (stance).
struct Choice {
    std::uint64_t node;
    Method method;
    float blend;
    const char* refusal;  // nullptr: node valid
};
Choice NodeFor(Kind kind, std::uint8_t stance, bool moving, bool hand) noexcept;

// --- Wire (reliable channel 2, append mode; the capability message, the ride
// packets and the manifest share the channel, each with its own first byte).
//   [0] 0x47 'G'  [1] version 1  [2] kind 1..4  [3] stance 0..3  [4] speed 0..3
//   [5] flags (bit 0 moving)  [6..7] seq LE16 (never 0)  [8..11] sender_ms LE32
inline constexpr std::uint8_t kChannel = 2, kMagic = 0x47, kVersion = 1;
inline constexpr std::size_t kSize = 12;
namespace Flag {
inline constexpr std::uint8_t Moving = 1 << 0;
inline constexpr std::uint8_t Known = 0x01;
}
struct Packet {
    Kind kind = Kind::none;
    std::uint8_t stance = 0, speed = 0, flags = 0;
    std::uint16_t seq = 0;
    std::uint32_t sender_ms = 0;
};
inline bool IsGesture(const std::uint8_t* b, std::size_t n) noexcept { return b && n && b[0] == kMagic; }
bool Encode(const Packet& packet, std::uint8_t (&out)[kSize]) noexcept;
// Strict: size, magic, version, kind 1..4, stance/speed 0..3, known flags, seq != 0.
bool Decode(const std::uint8_t* data, std::size_t size, Packet& out) noexcept;

// --- Sender: rising edges of the local upper act byte, sampled every tick.
struct SenderIn {
    bool enabled;       // the key is on and a link session is live
    bool valid;         // the upper byte, stance and speed were read this tick
    std::uint8_t upper, stance, speed;
    std::uint32_t epoch;  // link session (a new one re-baselines)
    std::uint64_t now_ms;
};
struct Sender {
    bool ready;              // a baseline sample exists
    std::uint8_t last_upper;
    std::uint32_t epoch;
    std::uint16_t seq;       // process-lifetime, never 0 on the wire
    std::uint32_t edges;     // edges sent (telemetry)
};
struct SenderOut {
    bool send;
    Packet packet;
};
// The first sample after enabling, after a read failure or in a new session is
// a baseline: a gesture already running then is not sent.
SenderOut Observe(Sender& sender, const SenderIn& in) noexcept;

// --- Receiver store: a strictly newer seq of the current session.
struct Store {
    bool have;
    Packet packet;
    std::uint32_t epoch;
    std::uint64_t received_ms;
    unsigned received, invalid, duplicates;
};
bool Newer(std::uint16_t seq, std::uint16_t prior) noexcept;
// True for a new packet (stored); a repeat or older seq of the same session is
// a duplicate, a malformed one invalid. A new session accepts any seq.
bool Accept(Store& store, const std::uint8_t* data, std::size_t size, std::uint32_t epoch, std::uint64_t now_ms) noexcept;

// Timing.
inline constexpr std::uint64_t kRequestMs = 500;      // a request the body-1 update has not taken by then is dropped
inline constexpr std::uint64_t kEndDeadlineMs = 2000; // partner_upper_actions::kEndDeadlineMs
inline constexpr std::uint64_t kMinPlayMs = 150;      // no clip-end test before this (a stale end flag of the previous node)
inline constexpr std::uint64_t kNodeGraceMs = 300;    // graph 2 may still show the previous node this long
inline constexpr std::uint64_t kLeaseMs = 250;        // the driver's command must be this fresh

// --- The driver's gate (before a request is published).
// Body 1 may take a gesture only in plain locomotion (act 1..6), with no upper
// action of its own (Q+0x20 == 0) and Q+0x28 == 0, like the stock pickup.
bool BodyReady(std::uint16_t act, std::uint8_t q28, std::uint8_t upper) noexcept;
struct GateIn {
    bool enabled;       // partner_native_gesture
    bool native;        // the native drive is active this tick
    bool realized;      // body 1 realized with its parts loaded
    bool remote_live;   // the remote's pose sample is live
    const char* block;  // carry / seat / overlay / downed / vehicle, or nullptr
    bool body_ok;       // body 1's act arrays were read
    std::uint16_t body_act;
    std::uint8_t body_q28, body_upper;
    bool installed;     // the body-1 update hook is live
    bool busy;          // a gesture is still running on body 1
    bool uncertain;     // the mission latch (a surprise in the viewer's own state)
};
// nullptr: publish the request; otherwise the refusal reason for the log.
const char* Gate(const GateIn& in) noexcept;

// --- The end of a running gesture (the body-1 update, before the original).
enum class Via : std::uint8_t { none, end, timeout, override_ };
const char* ViaName(Via via) noexcept;
struct EndIn {
    Kind kind;
    bool lease;          // the driver's command is fresh, enabled and of the same epoch
    bool identity;       // body 1's entry and controller are the ones the gesture was submitted on
    bool ours;           // graph 2's current or queued node is the gesture's node
    bool clip_end;       // pickup: vt+0x228(cs, 1, 2); sign / knock: vt+0x2B8(cs, 1, 3)
    bool remote_known;
    std::uint8_t remote_speed;  // the remote's Q+0x38 (sign / knock: > 1 ends, as stock)
    std::uint64_t elapsed_ms;
    // Body 1's request buffer already holds a graph-2 request this update has
    // not consumed (stock plugin code submits outside the motion update; the
    // update clears the masks only after consuming them): another owner takes
    // graph 2 now, and a stateNull would overwrite its request.
    bool pending;
    bool shutdown;       // the game window is closing (partner_gesture::Shutdown)
};
struct EndOut {
    Via via;
    const char* cause;   // identity | shutdown | pending | lease | node | clip | speed | deadline
    bool null_return;    // return graph 2 to stateNull (only while it still shows our node)
};
EndOut End(const EndIn& in) noexcept;

} // namespace mgscoop::partner_gesture_policy
