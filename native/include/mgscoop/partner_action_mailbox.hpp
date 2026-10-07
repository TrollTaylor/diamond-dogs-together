#pragma once
// Exact portable receive mailbox used by partner_net. Callers hold its lock.
// Trusted peer keys come only from the transport callback after endpoint/MAC
// acceptance. The payload never supplies identity. Latest history recovers up
// to eight events while the owner skips samples; larger gaps are explicit.
#include "mgscoop/partner_action_events.hpp"
#include "mgscoop/udp_transport.hpp"
namespace mgscoop::partner_actions {
inline std::uint64_t PeerKey(lab::Endpoint endpoint) noexcept { return lab::PackEndpoint(endpoint); }
inline bool TrustedPeer(bool v2, bool authenticated, lab::Endpoint from, lab::Endpoint configured, std::uint64_t& key) noexcept {
    if (!from.address || !from.port || (v2 ? !authenticated : !lab::SameEndpoint(from, configured))) return false;
    key = PeerKey(from); return true;
}
struct Mailbox {
    bool have = false; Frame latest{}; std::uint64_t peer = 0, rx_ms = 0; Receiver receiver{};
};
inline void Reset(Mailbox& m) noexcept { m = {}; }
inline bool Store(Mailbox& m, const Frame& f, std::uint64_t trusted_peer, std::uint64_t session,
                  std::uint32_t epoch, std::uint64_t now, std::uint64_t restart_gap = 2000) noexcept {
    if (!trusted_peer || !session || !epoch || f.session != session || f.epoch != epoch || !Valid(f.action)) return false;
    if (m.have && !Newer(f.sequence,m.latest.sequence)) {
        if (now < m.rx_ms || now - m.rx_ms <= restart_gap) return false;
        Reset(m); // allowed lab process restart/new owner baseline, never replay
    }
    // Even before owner consumption, context changes require a newer source
    // generation; malformed/reordered data cannot replace unconsumed history.
    if (m.have && f.action.generation != m.latest.action.generation && !Newer(f.action.generation,m.latest.action.generation)) return false;
    if (m.have && f.action.generation == m.latest.action.generation
        && (f.action.mission != m.latest.action.mission || f.action.equip != m.latest.action.equip
            || f.action.stance != m.latest.action.stance || f.action.fire_source != m.latest.action.fire_source
            || f.action.reload_source != m.latest.action.reload_source)) return false;
    if (m.have && f.action.generation == m.latest.action.generation) {
        const auto delta = f.action.cursor - m.latest.action.cursor;
        const auto fires = f.action.fire_count - m.latest.action.fire_count;
        const auto reloads = f.action.reload_count - m.latest.action.reload_count;
        if (f.action.tick_ms - m.latest.action.tick_ms >= 0x80000000u || delta >= 0x80000000u
            || fires >= 0x80000000u || reloads >= 0x80000000u || std::uint64_t(fires) + reloads != delta) return false;
        if (delta <= f.action.count) {
            unsigned retained_fires = 0, retained_reloads = 0;
            for (unsigned i = f.action.count - delta; i < f.action.count; ++i)
                if (f.action.history[i].kind == EventKind::fire) ++retained_fires; else ++retained_reloads;
            if (retained_fires != fires || retained_reloads != reloads) return false;
        }
    }
    m.have = true; m.latest = f; m.peer = trusted_peer; m.rx_ms = now; return true;
}
inline bool SampleMailbox(Mailbox& m, std::uint64_t session, std::uint32_t epoch, std::uint32_t mission,
                   std::uint64_t now, bool connected, Receipt& out, std::uint32_t& age) noexcept {
    out = {}; age = 0;
    if (!m.have || now < m.rx_ms) return false;
    const auto elapsed = now - m.rx_ms; age = elapsed > 0xffffffffu ? 0xffffffffu : static_cast<std::uint32_t>(elapsed);
    if (!connected || elapsed > kNetworkAgeMs || m.latest.session != session || m.latest.epoch != epoch || m.latest.action.mission != mission) return false;
    const Expected expected{m.peer, session, epoch, mission};
    // Lease is the actual receive time; owner sampling cannot freshen a stale
    // packet. Duplicate samples neither replay history nor renew the lease.
    out = Receive(m.receiver,m.latest,m.peer,expected,m.rx_ms);
    return out.status != ReceiptStatus::rejected;
}
} // namespace mgscoop::partner_actions
