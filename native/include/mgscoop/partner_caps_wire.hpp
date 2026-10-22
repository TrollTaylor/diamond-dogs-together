#pragma once
// Native puppet online (docs/native-puppet.md "Online"): the capability
// message each copy sends its partner on reliable channel 2 (append mode,
// next to the equipment manifest, which starts with 0x45), at every new link
// session, on change (at most once per second) and as a 5 s heartbeat. It
// tells the other PC how this copy shows the partner body and whether it
// sends the input frames the other copy's native drive needs. Pure codec
// (mgs_coop_core).
//
//   [0] 0x43 'C'  [1] version 1  [2..3] revision u16 (never 0)
//   [4..7] epoch u32 (the SENDER's partner_net link-session number: identity
//          only, never compared with the receiver's own counter; the store
//          binds the message to the receiver's own link session, link_epoch,
//          as the equipment manifest does: partner_equip_wire.hpp)
//   [8] features (Feature bits)  [9] profile (PartnerNativeProfile)
//   [10] drive (partner_native_net::Drive)  [11] reason code (partner_native_net::ReasonCode)
//   [12] arbiter mode (partner_native::Mode)  [13] net policy (PartnerNativeNet)  [14..15] 0
// Total 16 bytes, little-endian.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_caps_wire {

inline constexpr std::uint8_t kMagic = 0x43;
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kSize = 16;
inline constexpr std::uint8_t kChannel = 2;  // shared with partner_equip_wire (dispatch by magic)
// Cadence: on change at most this often, otherwise this heartbeat.
inline constexpr std::uint64_t kMinGapMs = 1000;
inline constexpr std::uint64_t kHeartbeatMs = 5000;

namespace Feature {
inline constexpr std::uint8_t InputSender = 1 << 0;   // partner_input_send: input frames v4 go out
inline constexpr std::uint8_t NativeDrive = 1 << 1;   // partner_drive=native configured
inline constexpr std::uint8_t NativeGun = 1 << 2;     // partner_native_gun
inline constexpr std::uint8_t Online = 1 << 3;        // partner_native_net=online
inline constexpr std::uint8_t HoldNet = 1 << 4;       // partner_native_hold=net
inline constexpr std::uint8_t StallHold = 1 << 5;     // partner_native_stall=hold
inline constexpr std::uint8_t Known = 0x3F;
}

struct Caps {
    std::uint16_t revision;
    std::uint32_t epoch;
    std::uint8_t features;
    std::uint8_t profile;   // 0 off, 1 loco, 2 gun
    std::uint8_t drive;     // partner_native_net::Drive
    std::uint8_t reason;    // partner_native_net::ReasonCode
    std::uint8_t mode;      // partner_native::Mode
    std::uint8_t policy;    // 0 lab, 1 online
};

// kSize, or 0 (out zeroed) for revision 0, epoch 0 or a field out of range.
std::size_t Encode(const Caps& caps, std::uint8_t (&out)[kSize]) noexcept;

// no_link: no receiver link session to bind it to; older: an older revision
// of the same sender epoch in the same receiver link session (Accept).
enum class Result : std::uint8_t { ok, size, magic, version, revision, epoch, stale_epoch, field, reserved, no_link, older };
const char* ResultName(Result result) noexcept;
// Strict decode; current_epoch != 0 drops other epochs (stale_epoch). A strict
// helper only: Accept passes 0.
Result Decode(const std::uint8_t* data, std::size_t size, std::uint32_t current_epoch, Caps& out) noexcept;
// The first byte of a channel-2 message names its codec.
inline bool IsCaps(const std::uint8_t* data, std::size_t size) noexcept { return data && size && data[0] == kMagic; }

// Receiver store (driver-owned; it outlives missions) and counters. caps.epoch
// is the sender's session number; link_epoch the receiver's own link session
// the stored message was drained in (the "live" test compares only that).
struct Store {
    bool have;
    Caps caps;
    std::uint32_t link_epoch;
    std::uint32_t received, invalid, stale;
    Result last;
};
// ok stores the message stamped with the receiver's link_epoch (newer or equal
// revisions of the same sender epoch in the same link session, or anything
// in a new link session) and returns true; anything else only counts.
bool Accept(Store& store, const std::uint8_t* data, std::size_t size, std::uint32_t link_epoch) noexcept;
// The stored capabilities are of the receiver's current link session.
inline bool Live(const Store& store, std::uint32_t link_session) noexcept {
    return store.have && link_session && store.link_epoch == link_session;
}

// Sender pacing: whether to send `next` now (a new epoch at once; a change at
// most every kMinGapMs; the same content every kHeartbeatMs).
struct Sender {
    bool sent_any;
    Caps last;            // revision/epoch as sent
    std::uint64_t sent_ms;
    std::uint16_t revision;
    std::uint32_t sent;
};
// True when `next` (revision and epoch filled by Due) should be sent; the
// caller calls Sent() after a successful queue.
bool Due(Sender& sender, Caps& next, std::uint32_t epoch, std::uint64_t now_ms) noexcept;
void Sent(Sender& sender, const Caps& caps, std::uint64_t now_ms) noexcept;

} // namespace mgscoop::partner_caps_wire
