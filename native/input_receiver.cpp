#include "mgscoop/input_receiver.hpp"
#include "mgscoop/typed_payload.hpp"

#include <type_traits>
#include <utility>

namespace mgscoop {
namespace {
static_assert(std::is_nothrow_move_assignable<PeerRouter>::value,
              "Router commit after Session acceptance must not throw");
static_assert(std::is_trivially_copyable<InputReceiveResult>::value,
              "Admission result must not allocate or throw on return");
static_assert(wire::header_size + typed::input_payload_size <= PeerRouter::kMaxPayloadBytes,
              "Input wire frame must fit the router");

InputReceiveResult reject(InputReceiveStatus status) noexcept {
    return {status, std::nullopt, std::nullopt};
}
} // namespace

InputReceiveResult receive_host_input(Session& session, PeerRouter& router,
                                      const HostInputContext& context,
                                      const PeerRouter::Bytes& frame_bytes) {
    // Bound untrusted data before wire decode or router copying can allocate.
    if (frame_bytes.size() != wire::header_size + typed::input_payload_size)
        return reject(InputReceiveStatus::invalid_frame);
    if (!context.session_id || !context.transport_source.id || !context.transport_source.generation)
        return reject(InputReceiveStatus::invalid_context);

    // This read-only query exists only for a currently bound exact PeerHandle.
    // Transport generations and Session slot generations are different domains.
    if (!router.buffered_event_count(context.transport_source).has_value())
        return reject(InputReceiveStatus::unbound_source);
    const auto assigned = session.token_for(context.transport_source.id);
    if (!assigned) return reject(InputReceiveStatus::unassigned_source);

    const auto packet = wire::decode(frame_bytes.data(), frame_bytes.size());
    if (!packet) return reject(InputReceiveStatus::invalid_frame);
    const auto input = typed::decode_input(*packet);
    if (!input) return reject(InputReceiveStatus::invalid_payload);
    if (packet->session != context.session_id) return reject(InputReceiveStatus::wrong_session);
    if (packet->epoch != session.checkpoint().epoch) return reject(InputReceiveStatus::wrong_epoch);
    if (input->token.slot != assigned->slot || input->token.generation != assigned->generation)
        return reject(InputReceiveStatus::wrong_slot);

    // LatestState receive updates its cursor before constructing its delivered
    // vector. Stage the whole router so rejection/allocation cannot consume a
    // sequence in the live router or discard unrelated reliable-event buffers.
    auto staged = router;
    const auto routed = staged.receive(context.transport_source, PeerRouter::DeliveryKind::LatestState,
                                       packet->sequence, frame_bytes);
    if (routed.status != PeerRouter::ReceiveStatus::Delivered)
        return {InputReceiveStatus::router_rejected, routed.status, std::nullopt};
    if (routed.delivered.size() != 1 ||
        !(routed.delivered[0].source == context.transport_source) ||
        routed.delivered[0].kind != PeerRouter::DeliveryKind::LatestState ||
        routed.delivered[0].sequence != packet->sequence || routed.delivered[0].payload != frame_bytes)
        return {InputReceiveStatus::router_contract_violation, routed.status, std::nullopt};

    // The exact delivered bytes have already passed typed/current-context checks.
    // Keep Session's own phase, sequence and host-clock gates as final authority.
    const auto admitted = session.accept_input(context.transport_source.id, input->token,
                                               input->frame, context.received_ms);
    if (admitted != Result::ok)
        return {InputReceiveStatus::session_rejected, routed.status, admitted};

    // No allocation, callback or throwing operation may follow Session success.
    router = std::move(staged);
    return {InputReceiveStatus::accepted, PeerRouter::ReceiveStatus::Delivered, Result::ok};
}

} // namespace mgscoop
