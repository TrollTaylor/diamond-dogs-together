#include "mgscoop/steam_packets.hpp"
#include "mgscoop/input_receiver.hpp"
#include "mgscoop/typed_payload.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace mgscoop;
unsigned checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)
using Bytes = PeerRouter::Bytes;
struct Message { std::uint64_t sender; std::int32_t channel; Bytes bytes; };
struct Fake {
    std::uint64_t id = 0x01100001abcdef12ull;
    unsigned sends = 0, peeks = 0, reads = 0;
    std::uint64_t last_recipient = 0;
    std::int32_t last_channel = -1, last_mode = -1;
    std::uint32_t last_capacity = 0;
    Bytes last_sent;
    std::deque<Message> queue;
    Fake* destination = nullptr;
    bool send_ok = true, read_ok = true;
    bool invalid_destination = false;
    std::optional<std::uint32_t> claimed_size;
    std::optional<std::uint64_t> claimed_sender;
    std::optional<std::uint32_t> advertised_size;
};
Fake& object(std::intptr_t instance) { return *reinterpret_cast<Fake*>(instance); }
extern "C" bool MGSCOOP_STEAM_CALL send_fake(std::intptr_t instance, std::uint64_t to,
    const void* bytes, std::uint32_t size, std::int32_t mode, std::int32_t channel) {
    auto& fake = object(instance);
    ++fake.sends;
    fake.last_recipient = to; fake.last_mode = mode; fake.last_channel = channel;
    const auto* first = static_cast<const std::uint8_t*>(bytes);
    fake.last_sent.assign(first, first + size);
    if (fake.send_ok && fake.destination) {
        if (fake.destination->id != to) { fake.invalid_destination = true; return false; }
        fake.destination->queue.push_back({fake.id, channel, fake.last_sent});
    }
    return fake.send_ok;
}
extern "C" bool MGSCOOP_STEAM_CALL available_fake(std::intptr_t instance, std::uint32_t* size, std::int32_t channel) {
    auto& fake = object(instance);
    ++fake.peeks; fake.last_channel = channel;
    const auto found = std::find_if(fake.queue.begin(), fake.queue.end(),
        [channel](const Message& message) { return message.channel == channel; });
    if (found == fake.queue.end()) return false;
    *size = fake.advertised_size.value_or(static_cast<std::uint32_t>(found->bytes.size()));
    return true;
}
extern "C" bool MGSCOOP_STEAM_CALL read_fake(std::intptr_t instance, void* buffer, std::uint32_t capacity,
    std::uint32_t* size, std::uint64_t* sender, std::int32_t channel) {
    auto& fake = object(instance);
    ++fake.reads; fake.last_channel = channel; fake.last_capacity = capacity;
    if (!fake.read_ok) { *size = 1200; *sender = fake.id; return false; }
    const auto found = std::find_if(fake.queue.begin(), fake.queue.end(),
        [channel](const Message& message) { return message.channel == channel; });
    if (found == fake.queue.end()) return false;
    const auto count = std::min<std::size_t>(found->bytes.size(), capacity);
    if (count) std::memcpy(buffer, found->bytes.data(), count);
    *size = fake.claimed_size.value_or(static_cast<std::uint32_t>(count));
    *sender = fake.claimed_sender.value_or(found->sender);
    fake.queue.erase(found);
    return true;
}
SteamPacketChannel access(Fake& fake) {
    return {SteamLifecycle::EngineInitialized, true, true, 17,
        reinterpret_cast<std::intptr_t>(&fake), {send_fake, available_fake, read_fake}};
}
PeerRouter::PeerHandle bind(PeerRouter& router, std::uint64_t id) {
    const auto result = router.bind_authenticated_peer(id, PeerRouter::AuthenticationEvidence::ValidatedByEngine);
    CHECK(result.peer.has_value());
    return *result.peer;
}
struct Resolver {
    int missing = -1;
    std::array<unsigned, 3> calls{};
    bool unexpected = false;
};
SteamExport resolve_fake(void* context, const char* name) noexcept {
    auto& resolver = *static_cast<Resolver*>(context);
    const std::array<const char*, 3> names{{"SteamAPI_ISteamNetworking_SendP2PPacket",
        "SteamAPI_ISteamNetworking_IsP2PPacketAvailable", "SteamAPI_ISteamNetworking_ReadP2PPacket"}};
    const std::array<SteamExport, 3> exports{{reinterpret_cast<SteamExport>(send_fake),
        reinterpret_cast<SteamExport>(available_fake), reinterpret_cast<SteamExport>(read_fake)}};
    for (std::size_t i = 0; i < names.size(); ++i) if (std::strcmp(name, names[i]) == 0) {
        ++resolver.calls[i];
        return resolver.missing == static_cast<int>(i) ? nullptr : exports[i];
    }
    resolver.unexpected = true;
    return nullptr;
}
void resolver_gates() {
    Resolver r;
    const SteamExportResolver source{true, &r, resolve_fake};
    for (auto lifecycle : {SteamLifecycle::Unconfirmed, SteamLifecycle::ShuttingDown, static_cast<SteamLifecycle>(99)})
        CHECK(!resolve_steam_packet_api(source, lifecycle, true).complete());
    CHECK(!resolve_steam_packet_api(source, SteamLifecycle::EngineInitialized, false).complete());
    CHECK(!resolve_steam_packet_api({false, &r, resolve_fake}, SteamLifecycle::EngineInitialized, true).complete());
    CHECK(!resolve_steam_packet_api({true, &r, nullptr}, SteamLifecycle::EngineInitialized, true).complete());
    CHECK(r.calls == (std::array<unsigned, 3>{0, 0, 0}));
    for (int missing = 0; missing < 3; ++missing) {
        r.missing = missing;
        const auto api = resolve_steam_packet_api(source, SteamLifecycle::EngineInitialized, true);
        CHECK(!api.send && !api.available && !api.read);
    }
    r.missing = -1;
    CHECK(resolve_steam_packet_api(source, SteamLifecycle::EngineInitialized, true).complete());
    CHECK(r.calls == (std::array<unsigned, 3>{4, 4, 4}) && !r.unexpected);
}
void access_and_send_gates() {
    Fake fake;
    PeerRouter peers;
    const auto peer = bind(peers, fake.id);
    const Bytes bytes{0, 255, 1};
    auto reject = [&](SteamPacketChannel a, SteamSendStatus sent, SteamPollStatus polled) {
        CHECK(send_steam_packet(a, peers, peer, SteamPacketDelivery::Reliable, bytes.data(), bytes.size()) == sent);
        const auto result = poll_steam_packet(a);
        CHECK(result.status == polled && !result.datagram);
        CHECK(fake.sends == 0 && fake.peeks == 0 && fake.reads == 0);
    };
    for (auto lifecycle : {SteamLifecycle::Unconfirmed, SteamLifecycle::ShuttingDown, static_cast<SteamLifecycle>(99)}) {
        auto a = access(fake); a.lifecycle = lifecycle;
        reject(a, SteamSendStatus::Gated, SteamPollStatus::Gated);
    }
    auto a = access(fake); a.interface_abi_verified = false;
    reject(a, SteamSendStatus::Gated, SteamPollStatus::Gated);
    a = access(fake); a.channel_ownership_verified = false;
    reject(a, SteamSendStatus::Gated, SteamPollStatus::Gated);
    for (auto channel : {-1, 0, std::numeric_limits<std::int32_t>::min()}) {
        a = access(fake); a.channel = channel;
        reject(a, SteamSendStatus::InvalidAccess, SteamPollStatus::InvalidAccess);
    }
    a = access(fake); a.instance = 0;
    reject(a, SteamSendStatus::InvalidAccess, SteamPollStatus::InvalidAccess);
    a = access(fake); a.api.send = nullptr;
    reject(a, SteamSendStatus::MissingApi, SteamPollStatus::MissingApi);
    a = access(fake); a.api.available = nullptr;
    reject(a, SteamSendStatus::MissingApi, SteamPollStatus::MissingApi);
    a = access(fake); a.api.read = nullptr;
    reject(a, SteamSendStatus::MissingApi, SteamPollStatus::MissingApi);
    a = access(fake);
    CHECK(send_steam_packet(a, peers, {}, SteamPacketDelivery::Reliable, bytes.data(), bytes.size()) == SteamSendStatus::UnboundPeer);
    CHECK(send_steam_packet(a, peers, peer, SteamPacketDelivery::Reliable, nullptr, 1) == SteamSendStatus::InvalidPayload);
    for (auto size : {std::size_t{0}, std::size_t{1201}, std::numeric_limits<std::size_t>::max()})
        CHECK(send_steam_packet(a, peers, peer, SteamPacketDelivery::Reliable, bytes.data(), size) == SteamSendStatus::InvalidPayload);
    CHECK(send_steam_packet(a, peers, peer, static_cast<SteamPacketDelivery>(99), bytes.data(), bytes.size()) == SteamSendStatus::InvalidDelivery);
    CHECK(peers.disconnect(peer));
    const auto replacement = bind(peers, fake.id);
    CHECK(send_steam_packet(a, peers, peer, SteamPacketDelivery::Reliable, bytes.data(), bytes.size()) == SteamSendStatus::UnboundPeer);
    CHECK(fake.sends == 0);
    Bytes full(1200);
    for (std::size_t i = 0; i < full.size(); ++i) full[i] = static_cast<std::uint8_t>(i);
    CHECK(send_steam_packet(a, peers, replacement, SteamPacketDelivery::ReplaceableNoDelay, full.data(), full.size()) == SteamSendStatus::ApiAccepted);
    CHECK(fake.last_recipient == fake.id && fake.last_mode == 1 && fake.last_channel == 17 && fake.last_sent == full);
    fake.send_ok = false;
    CHECK(send_steam_packet(a, peers, replacement, SteamPacketDelivery::Reliable, bytes.data(), bytes.size()) == SteamSendStatus::ApiRejected);
    CHECK(fake.last_mode == 2 && fake.sends == 2);
    CHECK(peers.buffered_event_count(replacement) == 0);
}
void polling_bounds_and_channel_isolation() {
    Fake fake;
    const auto a = access(fake);
    CHECK(poll_steam_packet(a).status == SteamPollStatus::Empty);
    CHECK(fake.peeks == 1 && fake.reads == 0);
    fake.queue.push_back({999, 0, {1, 2, 3}}); // Inert stand-in for another channel.
    CHECK(poll_steam_packet(a).status == SteamPollStatus::Empty && fake.queue.size() == 1);
    Bytes full(1200);
    for (std::size_t i = 0; i < full.size(); ++i) full[i] = static_cast<std::uint8_t>(i);
    fake.queue.push_back({fake.id, 17, full});
    fake.queue.push_back({fake.id + 1, 17, {4}});
    auto result = poll_steam_packet(a);
    CHECK(result.status == SteamPollStatus::ReceivedUnadmitted && result.datagram);
    CHECK(result.datagram->sender == fake.id && result.datagram->size == 1200);
    CHECK(std::equal(full.begin(), full.end(), result.datagram->bytes.begin()));
    CHECK(fake.last_capacity == 1200 && fake.queue.size() == 2 && fake.reads == 1);
    result = poll_steam_packet(a);
    CHECK(result.datagram && result.datagram->sender == fake.id + 1 && result.datagram->size == 1);
    CHECK(fake.queue.size() == 1 && fake.queue.front().channel == 0);
    fake.queue.push_back({fake.id, 17, Bytes(1201, 9)});
    const auto reads = fake.reads;
    result = poll_steam_packet(a);
    CHECK(result.status == SteamPollStatus::OversizedHead && result.advertised_size == 1201 && !result.datagram);
    CHECK(fake.reads == reads && fake.queue.size() == 2);
    fake.advertised_size = std::numeric_limits<std::uint32_t>::max();
    CHECK(poll_steam_packet(a).status == SteamPollStatus::OversizedHead && fake.reads == reads);

    for (const auto reported : {0u, 2u, 1201u, std::numeric_limits<std::uint32_t>::max()}) {
        Fake bad; bad.queue.push_back({bad.id, 17, {7}}); bad.claimed_size = reported;
        result = poll_steam_packet(access(bad));
        CHECK(result.status == SteamPollStatus::InvalidRead && !result.datagram && bad.reads == 1);
    }
    Fake zero; zero.queue.push_back({0, 17, {7}});
    CHECK(poll_steam_packet(access(zero)).status == SteamPollStatus::InvalidRead);
    Fake empty; empty.queue.push_back({empty.id, 17, {}});
    CHECK(poll_steam_packet(access(empty)).status == SteamPollStatus::InvalidRead && empty.queue.empty());
    Fake failed; failed.queue.push_back({failed.id, 17, {7}}); failed.read_ok = false;
    result = poll_steam_packet(access(failed));
    CHECK(result.status == SteamPollStatus::ReadFailed && !result.datagram && failed.reads == 1);
}
void three_guest_inputs_reach_real_coordinator_with_explicit_trust() {
    // Fake packet exports only: no DLL, sockets, Steam account or game process.
    Fake host;
    const BuildIdentity build{"fixture-exe", "fixture-content", 1};
    Session session(host.id, build);
    PeerRouter host_peers;
    std::array<Fake, 3> guests;
    std::array<PeerRouter, 3> guest_peers;
    std::array<PeerRouter::PeerHandle, 3> host_bindings, guest_bindings;
    for (std::size_t i = 0; i < guests.size(); ++i) {
        guests[i].id = host.id + i + 1; guests[i].destination = &host;
        CHECK(session.join(guests[i].id, build).result == Result::ok);
        host_bindings[i] = bind(guest_peers[i], host.id);
        guest_bindings[i] = bind(host_peers, guests[i].id);
    }
    CHECK(session.begin_load(host.id, 10010, 1) == Result::ok);
    for (const auto& player : session.players()) CHECK(session.mark_loaded(player.peer, 1) == Result::ok);
    CHECK(session.start_play(host.id) == Result::ok);
    constexpr std::uint64_t transport_session = 0xaabbccdd11223344ull;
    for (std::size_t i = 0; i < guests.size(); ++i) {
        auto packet = typed::encode_input(transport_session, *session.token_for(guests[i].id),
            {1, static_cast<std::uint32_t>(i), 0.25f * static_cast<float>(i + 1), -0.5f, 1});
        CHECK(packet);
        const auto bytes = wire::encode(*packet);
        CHECK(send_steam_packet(access(guests[i]), guest_peers[i], host_bindings[i],
            SteamPacketDelivery::ReplaceableNoDelay, bytes.data(), bytes.size()) == SteamSendStatus::ApiAccepted);
        CHECK(!guests[i].invalid_destination);
    }
    for (std::size_t i = 0; i < guests.size(); ++i) {
        const auto polled = poll_steam_packet(access(host));
        CHECK(polled.status == SteamPollStatus::ReceivedUnadmitted && polled.datagram);
        const auto& raw = *polled.datagram;
        CHECK(raw.sender == guests[i].id);
        // Fixture knows the admitted binding/session. Production must validate
        // its handshake and pre-rebind replay boundary before this handoff.
        const Bytes bytes(raw.bytes.begin(), raw.bytes.begin() + raw.size);
        const auto received = receive_host_input(session, host_peers,
            {guest_bindings[i], transport_session, 1000}, bytes);
        CHECK(received.status == InputReceiveStatus::accepted);
        const auto input = session.input_for(raw.sender, 1000);
        CHECK(input && input->move_x == 0.25f * static_cast<float>(i + 1));
        CHECK(!session.input_for(raw.sender, 1251));
    }
    CHECK(poll_steam_packet(access(host)).status == SteamPollStatus::Empty);
    CHECK(session.player_count() == 4 && host.reads == 3);
}
} // namespace
int main() {
    try {
        resolver_gates(); access_and_send_gates(); polling_bounds_and_channel_isolation();
        three_guest_inputs_reach_real_coordinator_with_explicit_trust();
        std::cout << "steam_packet_tests: " << checks << " gated packet I/O and coordinator checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "steam_packet_tests: " << error.what() << '\n';
        return 1;
    }
}
