#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>

namespace mgscoop {
constexpr std::size_t max_players = 4;
using PeerId = std::uint64_t;
enum class Phase { lobby, loading, playing, host_lost };
enum class Result { ok, unauthorized, full, incompatible, wrong_phase, stale,
                    invalid, not_ready, duplicate, exhausted };
struct BuildIdentity {
    std::string executable_sha256;
    std::string content_sha256;
    std::uint32_t protocol = 1;
    bool operator==(const BuildIdentity& rhs) const;
};
struct SlotToken {
    std::uint8_t slot = 0;
    std::uint32_t generation = 0;
};
struct InputFrame {
    std::uint32_t epoch = 0;
    std::uint32_t sequence = 0;
    float move_x = 0;
    float move_y = 0;
    std::uint16_t buttons = 0;
};
struct Player {
    PeerId peer = 0;
    std::uint32_t generation = 0;
    bool ready = false;
    bool has_input = false;
    InputFrame last_input{};
    std::uint64_t input_received_ms = 0;
};
struct Admission { Result result; std::optional<SlotToken> token; };
struct Checkpoint {
    std::uint32_t epoch = 0;
    std::uint32_t mission = 0;
    std::uint64_t checkpoint = 0;
    // A coordinator record only. This is NOT a FOX world snapshot or game save.
};

// Called on the engine main thread by an adapter that has authenticated PeerId.
// Network payloads must never supply the trusted caller identity themselves.
class Session {
public:
    Session(PeerId host, BuildIdentity build);
    Admission join(PeerId authenticated_peer, const BuildIdentity& build);
    Result leave(PeerId authenticated_peer);
    Result begin_load(PeerId caller, std::uint32_t mission, std::uint64_t checkpoint);
    Result mark_loaded(PeerId caller, std::uint32_t epoch);
    Result start_play(PeerId caller);
    Result accept_input(PeerId caller, SlotToken token, InputFrame input, std::uint64_t received_ms);
    // Trusted monotonic host clock; stale held controls return no input.
    std::optional<InputFrame> input_for(PeerId peer, std::uint64_t now_ms) const;
    Result complete_objective(PeerId caller, std::uint32_t epoch, std::uint64_t event, std::uint32_t objective);
    std::optional<SlotToken> token_for(PeerId peer) const;
    std::size_t player_count() const;
    Phase phase() const { return phase_; }
    Checkpoint checkpoint() const { return checkpoint_; }
    bool objective_complete(std::uint32_t id) const { return objectives_.count(id) != 0; }
    const std::array<Player, max_players>& players() const { return players_; }
private:
    Player* find(PeerId peer);
    PeerId host_;
    BuildIdentity build_;
    Phase phase_ = Phase::lobby;
    Checkpoint checkpoint_{};
    std::array<Player, max_players> players_{};
    std::set<std::uint64_t> events_;
    std::set<std::uint32_t> objectives_;
};
// The lab can exercise 4 slots. A production adapter needs independent evidence.
struct EngineCapabilities {
    bool executable_verified = false;
    bool hooks_verified = false;
    bool authenticated_transport_verified = false;
    bool mission_adapter_verified = false;
    std::uint8_t verified_playable_actors = 0;
};
std::string launch_blocker(const EngineCapabilities& caps, std::uint8_t requested);
bool newer_sequence(std::uint32_t incoming, std::uint32_t previous);
const char* result_name(Result result);
}
