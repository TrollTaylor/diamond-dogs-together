#include "mgscoop/session.hpp"
#include "mgscoop/protocol.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace mgscoop;
static int checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error("Check failed at line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)
static BuildIdentity build{"test-executable-fingerprint", "test-content-fingerprint", 1};
static void play(Session& s) {
    CHECK(s.begin_load(100, 10010, 1) == Result::ok);
    for (const auto& p : s.players()) if (p.peer) CHECK(s.mark_loaded(p.peer, s.checkpoint().epoch) == Result::ok);
    CHECK(s.start_play(100) == Result::ok);
}
int main() {
    try {
        Session s(100, build);
        CHECK(s.join(0, build).result == Result::invalid);
        auto mismatch = build; mismatch.content_sha256 = "wrong";
        CHECK(s.join(200, mismatch).result == Result::incompatible);
        CHECK(s.player_count() == 1);
        const auto guest = s.join(200, build);
        CHECK(guest.result == Result::ok && guest.token->slot == 1);
        CHECK(s.join(200, build).result == Result::duplicate);
        CHECK(s.join(300, build).result == Result::ok);
        CHECK(s.join(400, build).result == Result::ok);
        CHECK(s.join(500, build).result == Result::full);
        CHECK(s.player_count() == 4);
        CHECK(s.begin_load(200, 10010, 1) == Result::unauthorized);
        CHECK(s.begin_load(100, 10010, 1) == Result::ok);
        CHECK(s.start_play(100) == Result::not_ready);
        CHECK(s.mark_loaded(500, 1) == Result::unauthorized);
        CHECK(s.mark_loaded(200, 0) == Result::stale);
        CHECK(s.mark_loaded(100, 1) == Result::ok);
        CHECK(s.mark_loaded(200, 1) == Result::ok);
        CHECK(s.mark_loaded(300, 1) == Result::ok);
        CHECK(s.start_play(200) == Result::unauthorized);
        CHECK(s.start_play(100) == Result::not_ready);
        CHECK(s.mark_loaded(400, 1) == Result::ok);
        CHECK(s.start_play(100) == Result::ok);
        CHECK(s.accept_input(300, *guest.token, {1, 1, 0.5f, 0, 0}, 1000) == Result::unauthorized);
        CHECK(s.accept_input(200, *guest.token, {1, 1, 0.5f, 0, 0}, 1000) == Result::ok);
        CHECK(s.accept_input(200, *guest.token, {1, 1, 0.5f, 0, 0}, 1000) == Result::stale);
        CHECK(s.accept_input(300, *s.token_for(300), {1, 1, 0, 0.5f, 0}, 1000) == Result::ok);
        CHECK(s.accept_input(200, *guest.token, {1, 2, std::numeric_limits<float>::quiet_NaN(), 0, 0}, 1000) == Result::invalid);
        CHECK(s.accept_input(200, *guest.token, {1, 2, 0, 0, 0x8000}, 1000) == Result::invalid);
        CHECK(s.accept_input(200, *guest.token, {1, 2, 0, 0, 0}, 1000) == Result::ok);
        CHECK(s.input_for(200, 1250).has_value());
        CHECK(!s.input_for(200, 1251).has_value());
        CHECK(!s.input_for(200, 999).has_value());
        CHECK(s.accept_input(200, *guest.token, {1, 3, 0, 0, 0}, 999) == Result::invalid);
        CHECK(s.accept_input(200, *guest.token, {2, 3, 0, 0, 0}, 1000) == Result::stale);
        CHECK(s.complete_objective(200, 1, 1, 1) == Result::unauthorized);
        CHECK(s.complete_objective(100, 1, 1, 1) == Result::ok);
        CHECK(s.complete_objective(100, 1, 1, 2) == Result::duplicate);
        CHECK(s.complete_objective(100, 1, 2, 1) == Result::duplicate);
        CHECK(!s.objective_complete(2));
        CHECK(s.objective_complete(1));
        CHECK(s.begin_load(100, 10020, 2) == Result::ok);
        CHECK(s.checkpoint().epoch == 2);
        CHECK(!s.objective_complete(1));
        CHECK(s.accept_input(200, *guest.token, {1, 3, 0, 0, 0}, 1000) == Result::wrong_phase);
        CHECK(s.mark_loaded(200, 1) == Result::stale);
        CHECK(s.leave(400) == Result::ok);
        for (auto p : {100u, 200u, 300u}) CHECK(s.mark_loaded(p, 2) == Result::ok);
        CHECK(s.start_play(100) == Result::ok);
        CHECK(s.complete_objective(100, 1, 1, 1) == Result::stale);
        CHECK(!s.objective_complete(1));
        CHECK(s.join(500, build).result == Result::wrong_phase);
        CHECK(s.leave(100) == Result::ok);
        CHECK(s.phase() == Phase::host_lost);
        CHECK(s.accept_input(200, *guest.token, {2, 1, 0, 0, 0}, 1000) == Result::wrong_phase);
        CHECK(s.complete_objective(100, 2, 9, 9) == Result::wrong_phase);
        CHECK(s.join(600, build).result == Result::wrong_phase);

        Session recycle(100, build);
        const auto old = recycle.join(200, build).token.value();
        CHECK(recycle.leave(200) == Result::ok);
        const auto fresh = recycle.join(300, build).token.value();
        CHECK(old.slot == fresh.slot && old.generation != fresh.generation);
        play(recycle);
        CHECK(recycle.accept_input(300, old, {1, 1, 0, 0, 0}, 1000) == Result::unauthorized);
        CHECK(recycle.accept_input(300, fresh, {1, 0xfffffffeu, 0, 0, 0}, 1000) == Result::ok);
        CHECK(recycle.accept_input(300, fresh, {1, 0xffffffffu, 0, 0, 0}, 1000) == Result::ok);
        CHECK(recycle.accept_input(300, fresh, {1, 0, 0, 0, 0}, 1000) == Result::ok);
        CHECK(recycle.accept_input(300, fresh, {1, 0xffffffffu, 0, 0, 0}, 1000) == Result::stale);
        CHECK(!newer_sequence(0x80000000u, 0));

        EngineCapabilities caps;
        CHECK(!launch_blocker(caps, 4).empty());
        caps = {true, true, true, true, 2};
        CHECK(!launch_blocker(caps, 4).empty());
        CHECK(launch_blocker(caps, 2).empty());
        CHECK(!launch_blocker(caps, 5).empty());
        caps.verified_playable_actors = 4;
        CHECK(launch_blocker(caps, 4).empty());

        wire::Packet packet{wire::Kind::input, 0x0102030405060708ull, 2, 3, {1, 2, 3}};
        auto bytes = wire::encode(packet);
        CHECK(bytes.size() == 31 && bytes[8] == 8 && bytes[15] == 1);
        auto decoded = wire::decode(bytes.data(), bytes.size());
        CHECK(decoded && decoded->session == packet.session && decoded->payload == packet.payload &&
              decoded->kind == packet.kind && decoded->epoch == packet.epoch && decoded->sequence == packet.sequence);
        for (std::size_t length = 0; length < bytes.size(); ++length)
            CHECK(!wire::decode(bytes.data(), length));
        CHECK(!wire::decode(nullptr, bytes.size()));
        auto corrupt = bytes; corrupt[4] = 2;
        CHECK(!wire::decode(corrupt.data(), corrupt.size()));
        corrupt = bytes; corrupt[6] = 5;
        CHECK(!wire::decode(corrupt.data(), corrupt.size()));
        corrupt = bytes; corrupt[24] = 0xff;
        CHECK(!wire::decode(corrupt.data(), corrupt.size()));
        corrupt = bytes; corrupt.push_back(0);
        CHECK(!wire::decode(corrupt.data(), corrupt.size()));
        bool caught = false;
        try { packet.payload.resize(wire::max_payload + 1); wire::encode(packet); }
        catch (const std::invalid_argument&) { caught = true; }
        CHECK(caught);
        for (const auto kind : {wire::Kind::input, wire::Kind::snapshot, wire::Kind::event, wire::Kind::loaded}) {
            for (const auto length : {std::size_t(0), wire::max_payload}) {
                wire::Packet edge{kind, 42, 7, 0xffffffffu, std::vector<std::uint8_t>(length, 0xa5)};
                const auto encoded = wire::encode(edge);
                const auto roundtrip = wire::decode(encoded.data(), encoded.size());
                CHECK(roundtrip && roundtrip->kind == kind && roundtrip->payload == edge.payload &&
                      roundtrip->sequence == edge.sequence && roundtrip->epoch == 7 && roundtrip->session == 42);
            }
        }
        for (const auto offset : {8u, 16u}) {
            corrupt = bytes;
            const auto width = offset == 8 ? 8u : 4u;
            for (unsigned i = 0; i < width; ++i) corrupt[offset + i] = 0;
            CHECK(!wire::decode(corrupt.data(), corrupt.size()));
        }
        // Mutate valid headers to reach field validation beyond the magic check.
        for (unsigned value = 0; value <= 255; ++value) {
            corrupt = bytes; corrupt[4] = static_cast<std::uint8_t>(value);
            CHECK(wire::decode(corrupt.data(), corrupt.size()).has_value() == (value == 1));
            corrupt = bytes; corrupt[6] = static_cast<std::uint8_t>(value);
            CHECK(wire::decode(corrupt.data(), corrupt.size()).has_value() == (value >= 1 && value <= 4));
            corrupt = bytes; corrupt[24] = static_cast<std::uint8_t>(value);
            CHECK(wire::decode(corrupt.data(), corrupt.size()).has_value() == (value == 3));
        }
        // Deterministic malformed-input stress: arbitrary byte strings never overread or throw.
        std::uint32_t random = 123456;
        for (std::size_t n = 0; n < 2000; ++n) {
            std::vector<std::uint8_t> junk(n % 1100);
            for (auto& b : junk) { random = random * 1664525u + 1013904223u; b = static_cast<std::uint8_t>(random >> 24); }
            CHECK(!wire::decode(junk.data(), junk.size()));
        }
        std::cout << checks << " session/protocol checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
