#pragma once

#include "mgscoop/peer_router.hpp"
#include "mgscoop/steam_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mgscoop {

// Windows x64 machine-ABI bindings to the shipped flat exports. Valve's v1.33
// declarations use intptr_t for the instance and an eight-byte CSteamID value.
// Here its bits cross the verified x64 integer argument/output boundary. No
// proprietary class implementation or guessed virtual table is included.
// Exact DLL bytes, primary headers and scope: docs/steam-packet-adapter.md.
using SteamPacketSend = bool(MGSCOOP_STEAM_CALL*)(std::intptr_t, std::uint64_t,
    const void*, std::uint32_t, std::int32_t, std::int32_t);
using SteamPacketAvailable = bool(MGSCOOP_STEAM_CALL*)(std::intptr_t, std::uint32_t*, std::int32_t);
using SteamPacketRead = bool(MGSCOOP_STEAM_CALL*)(std::intptr_t, void*, std::uint32_t,
    std::uint32_t*, std::uint64_t*, std::int32_t);

struct SteamPacketApi {
    SteamPacketSend send = nullptr;
    SteamPacketAvailable available = nullptr;
    SteamPacketRead read = nullptr;
    bool complete() const noexcept { return send && available && read; }
};

// Resolves only the three names; never executes any resolved export. Returns an
// empty table unless all exist. The resolver must inspect a held, verified module
// of the exact supported ABI, and cannot load it or initialize Steam. No global
// cache is created. Unknown lifecycle/ABI makes zero resolver calls.
SteamPacketApi resolve_steam_packet_api(const SteamExportResolver& resolver,
    SteamLifecycle lifecycle, bool interface_abi_verified) noexcept;

// Borrow for ONE synchronous operation on the verified serialized Steam owner
// boundary. Do not cache this struct: its flags are trusted adapter assertions,
// not locks, authentication, or evidence acquired by this module. Initialization,
// shutdown, unload, peer changes and other reads of this channel cannot overlap.
// channel_ownership_verified must include exclusive receive ownership and proven
// non-conflict with the game. No default channel is chosen and channel 0 is refused.
struct SteamPacketChannel {
    SteamLifecycle lifecycle = SteamLifecycle::Unconfirmed;
    bool interface_abi_verified = false;
    bool channel_ownership_verified = false;
    std::int32_t channel = -1;
    std::intptr_t instance = 0;
    SteamPacketApi api{};
};

enum class SteamPacketDelivery { ReplaceableNoDelay, Reliable };
enum class SteamSendStatus { Gated, InvalidAccess, MissingApi, UnboundPeer,
    InvalidPayload, InvalidDelivery, ApiRejected, ApiAccepted };

// PeerHandle must be a current trusted admission. Sending can implicitly accept
// the underlying P2P session, so there is no arbitrary/unadmitted-recipient path.
// ApiAccepted is not delivery or application acknowledgement. Reliable events
// still need their own IDs/semantic commits. This function changes no router state.
SteamSendStatus send_steam_packet(const SteamPacketChannel& access,
    const PeerRouter& peers, PeerRouter::PeerHandle recipient,
    SteamPacketDelivery delivery, const std::uint8_t* bytes, std::size_t size);

constexpr std::size_t steam_packet_capacity = PeerRouter::kMaxPayloadBytes;
struct SteamRawDatagram {
    // SDK-reported identity only. No automatic binding generation is assigned:
    // delayed packets from before a rebind require handshake/replay validation.
    std::uint64_t sender = 0;
    std::uint32_t size = 0;
    std::array<std::uint8_t, steam_packet_capacity> bytes{};
};
enum class SteamPollStatus { Gated, InvalidAccess, MissingApi, Empty,
    OversizedHead, ReadFailed, InvalidRead, ReceivedUnadmitted };
struct SteamPollResult {
    SteamPollStatus status;
    std::uint32_t advertised_size = 0;
    std::optional<SteamRawDatagram> datagram;
};

// At most one availability call and one read. Fixed stack buffer; no allocation,
// internal loop, game channel drain, authentication, callback pump or peer close.
// OversizedHead does not consume the packet: the owner must suspend/quarantine
// this channel, not spin on it. Other read failures may have consumed a packet.
// Only ReceivedUnadmitted exposes bytes. Caller must validate handshake/session,
// source admission, framing and payload before applying anything to the core.
SteamPollResult poll_steam_packet(const SteamPacketChannel& access);

} // namespace mgscoop
