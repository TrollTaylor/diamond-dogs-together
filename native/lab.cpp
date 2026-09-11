#include "mgscoop/session.hpp"
#include <iostream>
#include <stdexcept>
using namespace mgscoop;
static void require(Result actual, Result expected) {
    if (actual != expected) throw std::runtime_error(result_name(actual));
}
int main() {
    try {
        std::cout << "MGS CO-OP development lab: in-process coordinator only; no game or Steam connection.\n";
        BuildIdentity build{"lab-executable", "lab-content", 1};
        Session session(1, build);
        for (PeerId peer = 2; peer <= 4; ++peer) require(session.join(peer, build).result, Result::ok);
        require(session.join(5, build).result, Result::full);
        std::cout << "PASS: four members admitted; fifth member rejected.\n";
        require(session.begin_load(1, 10010, 1), Result::ok);
        require(session.start_play(1), Result::not_ready);
        for (PeerId peer = 1; peer <= 4; ++peer) require(session.mark_loaded(peer, 1), Result::ok);
        require(session.start_play(1), Result::ok);
        std::cout << "PASS: mission release waits for all four load acknowledgements.\n";
        for (std::uint32_t tick = 1; tick <= 600; ++tick) {
            for (PeerId peer = 1; peer <= 4; ++peer) {
                if (tick % 7 == 0) continue; // Deliberately dropped input; next input supersedes it.
                const auto token = session.token_for(peer).value();
                require(session.accept_input(peer, token, {1, tick, .25f, 0, 0}, 1000), Result::ok);
                require(session.accept_input(peer, token, {1, tick, .25f, 0, 0}, 1000), Result::stale);
            }
        }
        require(session.complete_objective(2, 1, 1, 1), Result::unauthorized);
        require(session.complete_objective(1, 1, 1, 1), Result::ok);
        require(session.complete_objective(1, 1, 1, 1), Result::duplicate);
        std::cout << "PASS: independent input streams tolerate dropped updates and reject duplicates; only host commits objectives.\n";
        require(session.leave(1), Result::ok);
        require(session.accept_input(2, session.token_for(2).value(), {1, 601, 0, 0, 0}, 1000), Result::wrong_phase);
        std::cout << "PASS: host loss freezes campaign coordination. No host migration is claimed.\n";
        std::cout << "IN-GAME FOUR-PLAYER SUPPORT: UNVERIFIED; engine adapter is still required.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
