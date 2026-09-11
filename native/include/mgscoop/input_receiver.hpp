#pragma once

#include "mgscoop/peer_router.hpp"
#include "mgscoop/session.hpp"

#include <cstdint>
#include <optional>

namespace mgscoop {

// All fields come from the trusted host adapter, never from message contents.
// session_id identifies this Session's current transport session. received_ms is
// the host's monotonic receive clock, not a sender timestamp or an engine tick.
// The live Session supplies the source's slot/generation and current epoch.
struct HostInputContext {
    PeerRouter::PeerHandle transport_source{};
    std::uint64_t session_id = 0;
    std::uint64_t received_ms = 0;
};

enum class InputReceiveStatus {
    accepted,
    invalid_context,
    unbound_source,
    unassigned_source,
    invalid_frame,
    invalid_payload,
    wrong_session,
    wrong_epoch,
    wrong_slot,
    router_rejected,
    router_contract_violation,
    session_rejected
};

struct InputReceiveResult {
    InputReceiveStatus status;
    // Present only if that component was reached. A staged router delivery is
    // not a committed delivery when the overall result is session_rejected.
    std::optional<PeerRouter::ReceiveStatus> router_status;
    std::optional<Result> session_result;
};

// Narrow development integration for one complete typed v1 input wire frame.
// The caller MUST exclusively own both objects for this entire call, including
// admission, disconnects, load transitions and all other routing. No concurrent
// observers or reentrant mutation are supported; this is not a lock or an atomic
// multi-object transaction. The adapter still owns authentication and transport.
//
// Exact input length is checked before copying. Framing, typed values, current
// slot/epoch and live connection generation are checked before staging routing.
// Rejection leaves both live objects unchanged. A private router copy absorbs
// any routing mutation/allocation; Session::accept_input is the final admission
// gate. Its current rejection paths do not mutate, and its success path performs
// only fixed-field writes. Successful admission is followed only by a noexcept
// router move and trivial result construction. Re-audit this contract if either
// component changes. Allocation failure before admission propagates unchanged.
//
// Copying the router also copies bounded queued reliable events for other peers;
// this is a correctness bridge, not evidence of production input throughput.
// LatestState sequence continues across message kinds and epochs on each live
// binding; reliable events have their own independent sequence. No
// routing is performed before this call, and accepted input is already handed
// to Session; do not route or apply it a second time. Held-input expiry remains
// Session::input_for's responsibility. No sockets, Steam calls or actors exist
// in this receiver.
InputReceiveResult receive_host_input(Session& session, PeerRouter& router,
                                      const HostInputContext& context,
                                      const PeerRouter::Bytes& frame_bytes);

} // namespace mgscoop
