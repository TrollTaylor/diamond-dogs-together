#include "mgscoop/steam_packets.hpp"

namespace mgscoop {
namespace {
#if defined(_WIN32)
static_assert(sizeof(void*) == 8, "Live Steam packet ABI supports Windows x64 only");
#endif
static_assert(steam_packet_capacity == 1200, "Review Steam send limits when changing the router capacity");
enum class Access { Ready, Gated, Invalid, Missing };
Access check(const SteamPacketChannel& access) noexcept {
    if (access.lifecycle != SteamLifecycle::EngineInitialized ||
        !access.interface_abi_verified || !access.channel_ownership_verified) return Access::Gated;
    if (!access.instance || access.channel <= 0) return Access::Invalid;
    if (!access.api.complete()) return Access::Missing;
    return Access::Ready;
}
} // namespace

SteamPacketApi resolve_steam_packet_api(const SteamExportResolver& resolver,
    SteamLifecycle lifecycle, bool interface_abi_verified) noexcept {
    if (lifecycle != SteamLifecycle::EngineInitialized || !interface_abi_verified ||
        !resolver.module_loaded || !resolver.resolve) return {};
    SteamPacketApi api;
    api.send = reinterpret_cast<SteamPacketSend>(resolver.resolve(resolver.context,
        "SteamAPI_ISteamNetworking_SendP2PPacket"));
    api.available = reinterpret_cast<SteamPacketAvailable>(resolver.resolve(resolver.context,
        "SteamAPI_ISteamNetworking_IsP2PPacketAvailable"));
    api.read = reinterpret_cast<SteamPacketRead>(resolver.resolve(resolver.context,
        "SteamAPI_ISteamNetworking_ReadP2PPacket"));
    return api.complete() ? api : SteamPacketApi{};
}

SteamSendStatus send_steam_packet(const SteamPacketChannel& access,
    const PeerRouter& peers, PeerRouter::PeerHandle recipient,
    SteamPacketDelivery delivery, const std::uint8_t* bytes, std::size_t size) {
    switch (check(access)) {
        case Access::Gated: return SteamSendStatus::Gated;
        case Access::Invalid: return SteamSendStatus::InvalidAccess;
        case Access::Missing: return SteamSendStatus::MissingApi;
        case Access::Ready: break;
    }
    if (!peers.buffered_event_count(recipient).has_value()) return SteamSendStatus::UnboundPeer;
    if (!bytes || size == 0 || size > steam_packet_capacity) return SteamSendStatus::InvalidPayload;
    std::int32_t mode = 0;
    switch (delivery) {
        case SteamPacketDelivery::ReplaceableNoDelay: mode = 1; break;
        case SteamPacketDelivery::Reliable: mode = 2; break;
        default: return SteamSendStatus::InvalidDelivery;
    }
    return access.api.send(access.instance, recipient.id, bytes, static_cast<std::uint32_t>(size),
        mode, access.channel) ? SteamSendStatus::ApiAccepted : SteamSendStatus::ApiRejected;
}

SteamPollResult poll_steam_packet(const SteamPacketChannel& access) {
    switch (check(access)) {
        case Access::Gated: return {SteamPollStatus::Gated, 0, {}};
        case Access::Invalid: return {SteamPollStatus::InvalidAccess, 0, {}};
        case Access::Missing: return {SteamPollStatus::MissingApi, 0, {}};
        case Access::Ready: break;
    }
    std::uint32_t advertised = 0;
    if (!access.api.available(access.instance, &advertised, access.channel))
        return {SteamPollStatus::Empty, 0, {}};
    if (advertised > steam_packet_capacity) return {SteamPollStatus::OversizedHead, advertised, {}};
    SteamRawDatagram data;
    if (!access.api.read(access.instance, data.bytes.data(), static_cast<std::uint32_t>(data.bytes.size()),
        &data.size, &data.sender, access.channel)) return {SteamPollStatus::ReadFailed, advertised, {}};
    // Exact head size must survive the availability/read pair under exclusive
    // ownership. Never expose truncated, empty, inconsistent or senderless data.
    if (data.size == 0 || data.size > data.bytes.size() || data.size != advertised || !data.sender)
        return {SteamPollStatus::InvalidRead, advertised, {}};
    return {SteamPollStatus::ReceivedUnadmitted, advertised, data};
}

} // namespace mgscoop
