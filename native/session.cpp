#include "mgscoop/session.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace mgscoop {
bool BuildIdentity::operator==(const BuildIdentity& rhs) const {
    return protocol == rhs.protocol && executable_sha256 == rhs.executable_sha256 &&
           content_sha256 == rhs.content_sha256;
}
bool newer_sequence(std::uint32_t incoming, std::uint32_t previous) {
    const auto delta = incoming - previous;
    return delta != 0 && delta < 0x80000000u;
}
Session::Session(PeerId host, BuildIdentity build) : host_(host), build_(std::move(build)) {
    if (host == 0 || build_.executable_sha256.empty() || build_.content_sha256.empty())
        throw std::invalid_argument("A host and build fingerprints are required");
    players_[0].peer = host;
    players_[0].generation = 1;
}
Player* Session::find(PeerId peer) {
    if (!peer) return nullptr;
    for (auto& player : players_) if (player.peer == peer) return &player;
    return nullptr;
}
std::optional<SlotToken> Session::token_for(PeerId peer) const {
    if (!peer) return std::nullopt;
    for (std::size_t i = 0; i < players_.size(); ++i)
        if (players_[i].peer == peer)
            return SlotToken{static_cast<std::uint8_t>(i), players_[i].generation};
    return std::nullopt;
}
std::size_t Session::player_count() const {
    std::size_t count = 0;
    for (const auto& p : players_) if (p.peer) ++count;
    return count;
}
Admission Session::join(PeerId peer, const BuildIdentity& build) {
    if (!peer) return {Result::invalid, {}};
    if (!(build == build_)) return {Result::incompatible, {}};
    if (phase_ == Phase::host_lost) return {Result::wrong_phase, {}};
    if (auto existing = token_for(peer)) return {Result::duplicate, existing};
    // Late join requires a complete, acknowledged FOX baseline, not yet implemented.
    if (phase_ != Phase::lobby) return {Result::wrong_phase, {}};
    for (std::size_t i = 1; i < players_.size(); ++i) {
        auto& p = players_[i];
        if (!p.peer && p.generation != std::numeric_limits<std::uint32_t>::max()) {
            const auto generation = p.generation + 1;
            p = Player{};
            p.peer = peer;
            p.generation = generation;
            return {Result::ok, SlotToken{static_cast<std::uint8_t>(i), generation}};
        }
    }
    return {Result::full, {}};
}
Result Session::leave(PeerId peer) {
    auto* p = find(peer);
    if (!p) return Result::unauthorized;
    if (peer == host_) {
        phase_ = Phase::host_lost;
        for (auto& player : players_) { player.has_input = false; player.ready = false; }
    }
    const auto generation = p->generation;
    *p = Player{};
    p->generation = generation;
    return Result::ok;
}
Result Session::begin_load(PeerId caller, std::uint32_t mission, std::uint64_t checkpoint) {
    if (caller != host_) return Result::unauthorized;
    if (phase_ != Phase::lobby && phase_ != Phase::playing) return Result::wrong_phase;
    if (mission == 0 || checkpoint == 0) return Result::invalid;
    if (checkpoint_.epoch == std::numeric_limits<std::uint32_t>::max()) return Result::exhausted;
    checkpoint_ = {checkpoint_.epoch + 1, mission, checkpoint};
    phase_ = Phase::loading;
    events_.clear();
    objectives_.clear();
    for (auto& p : players_) { p.ready = false; p.has_input = false; p.last_input = {}; }
    return Result::ok;
}
Result Session::mark_loaded(PeerId caller, std::uint32_t epoch) {
    auto* p = find(caller);
    if (!p) return Result::unauthorized;
    if (phase_ != Phase::loading) return Result::wrong_phase;
    if (epoch != checkpoint_.epoch) return Result::stale;
    if (p->ready) return Result::duplicate;
    p->ready = true;
    return Result::ok;
}
Result Session::start_play(PeerId caller) {
    if (caller != host_) return Result::unauthorized;
    if (phase_ != Phase::loading) return Result::wrong_phase;
    for (const auto& p : players_) if (p.peer && !p.ready) return Result::not_ready;
    phase_ = Phase::playing;
    return Result::ok;
}
Result Session::accept_input(PeerId caller, SlotToken token, InputFrame input, std::uint64_t received_ms) {
    if (token.slot >= players_.size()) return Result::invalid;
    auto& p = players_[token.slot];
    if (!caller || p.peer != caller || p.generation != token.generation) return Result::unauthorized;
    if (phase_ != Phase::playing) return Result::wrong_phase;
    if (input.epoch != checkpoint_.epoch) return Result::stale;
    if (!std::isfinite(input.move_x) || !std::isfinite(input.move_y) ||
        std::abs(input.move_x) > 1.0f || std::abs(input.move_y) > 1.0f || (input.buttons & 0xff00u))
        return Result::invalid;
    if (p.has_input && !newer_sequence(input.sequence, p.last_input.sequence)) return Result::stale;
    if (p.has_input && received_ms < p.input_received_ms) return Result::invalid;
    p.last_input = input;
    p.input_received_ms = received_ms;
    p.has_input = true;
    return Result::ok;
}
std::optional<InputFrame> Session::input_for(PeerId peer, std::uint64_t now_ms) const {
    if (phase_ != Phase::playing) return {};
    const auto token = token_for(peer);
    if (!token) return {};
    const auto& player = players_[token->slot];
    if (!player.has_input || now_ms < player.input_received_ms || now_ms - player.input_received_ms > 250) return {};
    return player.last_input;
}
Result Session::complete_objective(PeerId caller, std::uint32_t epoch, std::uint64_t event, std::uint32_t objective) {
    if (caller != host_) return Result::unauthorized;
    if (phase_ != Phase::playing) return Result::wrong_phase;
    if (epoch != checkpoint_.epoch) return Result::stale;
    if (event == 0 || objective == 0) return Result::invalid;
    if (events_.count(event) || objectives_.count(objective)) return Result::duplicate;
    if (events_.size() >= 4096) return Result::exhausted;
    events_.insert(event);
    objectives_.insert(objective);
    return Result::ok;
}
std::string launch_blocker(const EngineCapabilities& caps, std::uint8_t requested) {
    if (requested < 1 || requested > max_players) return "Player count must be between 1 and 4";
    if (!caps.executable_verified) return "Executable fingerprint has not been verified";
    if (!caps.hooks_verified) return "Native hooks have not been verified for this executable";
    if (!caps.authenticated_transport_verified) return "Authenticated game transport has not been verified";
    if (requested > caps.verified_playable_actors) return "Requested playable actor count has not been demonstrated in the engine";
    if (!caps.mission_adapter_verified) return "This campaign segment has no verified co-op adapter";
    return {};
}
const char* result_name(Result result) {
    switch (result) {
    case Result::ok: return "ok";
    case Result::unauthorized: return "unauthorized";
    case Result::full: return "full";
    case Result::incompatible: return "incompatible";
    case Result::wrong_phase: return "wrong_phase";
    case Result::stale: return "stale";
    case Result::invalid: return "invalid";
    case Result::not_ready: return "not_ready";
    case Result::duplicate: return "duplicate";
    case Result::exhausted: return "exhausted";
    }
    return "unknown";
}
}
