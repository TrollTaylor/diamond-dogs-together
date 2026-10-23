# Typed player payload format

Updated 2026-09-09. The original portable codec supports player input and a complete player-pose roster using the existing `input` and `snapshot` wire kinds. The new host input receiver connects framing, typed validation, routing and session admission; its build and test results are pending. The codec's earlier verification is recorded below. Transport authentication, Steam delivery, engine-thread access, actor creation, and playable co-op are outside these components.

Sources: [codec types and contracts](../native/include/mgscoop/typed_payload.hpp), [codec implementation](../native/typed_payload.cpp), [codec tests](../tests/typed_payload_tests.cpp), [receiver contract](../native/include/mgscoop/input_receiver.hpp), [receiver implementation](../native/input_receiver.cpp), [receiver tests](../tests/input_receiver_tests.cpp), [wire framing](../native/protocol.cpp), [router](../native/include/mgscoop/peer_router.hpp), and [session coordinator](../native/session.cpp).

## Envelope and encoding

The existing `wire::Packet` envelope is 28 bytes. All multi-byte integers are explicitly little endian. The four-byte magic is the ASCII byte sequence `MGSC`.

| Envelope offset | Width | Field | Accepted value |
| --- | --- | --- | --- |
| 0 | 4 | Magic | `MGSC` |
| 4 | 2 | Wire version | `1` |
| 6 | 2 | Kind | `1` input or `2` snapshot for these codecs |
| 8 | 8 | Session identifier | Nonzero |
| 16 | 4 | Epoch | Nonzero |
| 20 | 4 | Sequence | Any unsigned 32-bit value, including zero |
| 24 | 4 | Payload length | Exact number of following bytes |

The framing layer also declares `event` and `loaded`, but this typed codec implements no payload format for either. Its decoders reject those kinds.

Every implemented payload starts with a schema discriminator and a separate payload version, currently `1`. These identify the payload independently of the envelope kind. A future richer snapshot format must not be interpreted as this pose roster. Unknown schemas or versions are rejected; no version negotiation is implemented here.

Floats use IEEE-754 binary32 bits written little endian. The code requires a four-byte IEEE-754 `float`, copies its bits without aliasing or alignment assumptions, and compares unsigned magnitudes to enforce bounds. Infinities and quiet/signaling NaNs are rejected. The format contains no compiler padding, native pointer, peer identity, authentication assertion, or client-supplied receive timestamp.

## Player input: schema 1, version 1

An input payload is exactly 17 bytes; its complete wire frame is 45 bytes.

| Payload offset | Width | Field | Validation |
| --- | --- | --- | --- |
| 0 | 1 | Schema | `1`, player input |
| 1 | 1 | Payload version | `1` |
| 2 | 1 | Slot | `0` through `3` |
| 3 | 4 | Slot generation | Nonzero unsigned 32-bit integer |
| 7 | 4 | Move X | Finite, within `[-1, 1]` |
| 11 | 4 | Move Y | Finite, within `[-1, 1]` |
| 15 | 2 | Buttons | Only bits `0..7` permitted; mask `0x00ff` |

Each movement component is checked independently, matching `Session::accept_input`; this codec does not normalize a diagonal vector. The allowed button bits have no engine action mapping in this format. Unknown high bits are rejected rather than silently discarded.

`encode_input` takes the envelope session identifier, a `SlotToken`, and an `InputFrame`. The frame's epoch and sequence are stored only in the envelope. `decode_input` reconstructs them from that envelope. Invalid header values, kinds, lengths, schemas, versions, slots, generations, movement values, or button bits return `std::nullopt`.

For example, slot `3`, generation `0x01020304`, movement `(0.5, -1)`, and buttons `0x00a5` produce these payload bytes:

```text
01 01 03 04 03 02 01 00 00 00 3F 00 00 80 BF A5 00
```

## Player-pose roster: schema 2, version 1

The snapshot kind currently carries a complete pose roster. It is not a FOX world snapshot, checkpoint, save, or actor-creation instruction. A position passing the codec's checks does not establish actor presence, freshness, coordinate space, or engine units.

| Payload offset | Width | Field | Validation |
| --- | --- | --- | --- |
| 0 | 1 | Schema | `2`, player-pose roster |
| 1 | 1 | Payload version | `1` |
| 2 | 1 | Entry count | `1` through `4` |
| 3 | `17 × count` | Entries | Strictly ascending slot order |

Each entry starts at payload offset `3 + 17 × entry_index`:

| Entry-relative offset | Width | Field | Validation |
| --- | --- | --- | --- |
| 0 | 1 | Slot | `0` through `3` |
| 1 | 4 | Slot generation | Nonzero unsigned 32-bit integer |
| 5 | 4 | Position X | Finite, within `[-1,000,000, 1,000,000]` |
| 9 | 4 | Position Y | Same bound |
| 13 | 4 | Position Z | Same bound |

The position bound is a defensive codec limit, not a measured MGSV map extent or unit conversion. Duplicate and out-of-order slots are rejected. Payload length must equal `3 + 17 × count`, giving 20–71 payload bytes and 48–99 bytes including the envelope. Truncation, extra bytes, zero count, and a fifth entry are rejected. Only the first `count` entries of the C++ fixed array are serialized; the unused tail has no wire representation.

The complete-roster requirement follows the current router's single latest-state sequence cursor per peer. Sending each slot independently could allow a newer slot update to discard an older packet for a different slot. Each accepted roster instead contains every currently active slot, so advancing the cursor selects a complete set of poses. It does not make the positions an atomic engine observation.

## Trusted receive boundary

The low-level typed decoders establish only that bytes fit a schema. A structurally valid slot and generation do not establish permission to control that slot.

`input_from_routed` and `pose_roster_from_routed` accept `PeerRouter::Packet` values delivered by the router. The router packet's payload must contain a complete wire frame. Both helpers require:

- `LatestState` delivery, a nonzero expected peer ID and binding generation, and exact agreement with the trusted source handle.
- The expected nonzero session identifier and epoch.
- Exact agreement between the router sequence and the sequence inside the wire envelope.
- A valid typed payload of the expected kind.

The adapter must construct receive contexts from current trusted admission state. It must not copy identity or generation claims from the received body into those contexts. Router binding generations are 64-bit values; session slot generations are independent 32-bit values. They are checked in their respective domains and never equated.

For input, `InputReceiveContext` supplies the authenticated source binding and that peer's current assigned slot token. The decoded token must match both the assigned slot and its generation. A peer cannot obtain another player's slot by encoding that player's token or appending a claimed peer ID. Old queued work from a replaced transport binding or recycled slot is rejected against the current context.

For poses, `PoseReceiveContext` supplies the authoritative host binding and the current generation for each active slot. A zero generation means inactive, and slot 0 must contain the current host. The received count must equal the active count, and every unique slot must match its trusted generation. This establishes exact roster coverage; the packet cannot admit a new slot, remove an active slot, or replace an admission generation. Empty rosters and a missing host are not represented by this schema.

## Router cursor and session handoff

The existing router advances its `LatestState` cursor when it delivers a packet. The post-delivery typed helpers cannot undo that cursor change if validation subsequently fails. Calling `router.receive` on live state and then `input_from_routed` can therefore let malformed traffic advance its own sender's cursor. Those helpers remain useful validation utilities; they do not provide transactional admission. The new `receive_host_input` is the preferred input path because it validates before committing live routing state. It handles input only; no equivalent receiver for pose rosters or reliable events is implemented.

For each binding, all messages sent through `LatestState` share one sequence cursor, even across different wire kinds and mission epochs. The adapter must maintain that sequence continuously; a load barrier does not reset it. `ReliableOrderedEvent` has an independent sequence and queue for each binding, starting from the value supplied during trusted binding setup. Latest-state input sequences must not be used to advance that reliable-event sequence. Router sequence handling and `Session` input sequence handling also remain separate checks: a newly bound router does not erase the Session's existing input replay history.

The host adapter supplies a currently bound transport handle, the nonzero transport session ID associated with this exact `Session`, and its own monotonic receive time. These must come from trusted connection/admission state and the host clock, never from decoded claims. `receive_host_input` derives the assigned slot/generation and current epoch from the live Session. It does not authenticate the connection, obtain a Steam identity, establish clock trust, or prove an engine-owner thread.

Preferred coordinator usage, while the caller exclusively owns both objects:

```cpp
const mgscoop::HostInputContext context{
    trusted_connection_handle, active_transport_session_id, host_receive_time_ms
};
const auto result = mgscoop::receive_host_input(session, router, context,
                                               complete_wire_frame);
if (result.status != mgscoop::InputReceiveStatus::accepted) {
    return; // No live input or routing change was committed.
}
// Session already holds this input. Do not route or accept it a second time.
```

`complete_wire_frame` is a `PeerRouter::Bytes` value containing one complete frame, not a previously delivered router packet. The receiver requires exactly 45 bytes before any copy or decoding allocation. It checks the live source binding, current assigned slot, wire framing, typed schema/values, transport session, mission epoch and slot generation. A structurally valid claim to another peer's slot is rejected before routing is staged.

Only after those checks does the receiver copy the bounded router and perform `LatestState` admission on that private copy. It verifies the resulting delivery contains the exact source, sequence, kind and bytes, then calls the real `Session::accept_input` as the final gate. The current Session implementation checks ownership, phase, epoch, numeric fields, sequence freshness and host receive-time ordering before changing any field. Every rejection path leaves Session unchanged; success writes only `last_input`, `input_received_ms` and `has_input`. These exact implementation properties are required by the receiver and must be re-audited if Session changes.

If Session rejects, the private router is discarded. If it accepts, the receiver commits the router through a move assignment statically required to be `noexcept`, then returns a trivial result. All allocating work occurs before Session admission. Allocation failure propagates to the caller with both live objects unchanged; no allocation, callback or throwing operation is permitted after successful Session admission. An optional `router_status` of `Delivered` with overall `session_rejected` describes the discarded staged delivery. Only overall `accepted` means input and routing were committed.

The entire operation requires exclusive serialized ownership of Session and Router, including other routing, admission, disconnects and load transitions. It supplies no synchronization and is not an atomic transaction for concurrent observers or reentrant callers. Copying the router also copies bounded reliable-event buffers for other peers. This preserves those queues under the ownership contract, but adds copying and allocation; no production input-rate or latency result is claimed.

`Session::input_for` still expires held input after 250 milliseconds and refuses a query time before receipt. The receiver's success does not apply movement to MGSV. Connecting the coordinator to game input still requires an independently verified engine-owner boundary and actor adapter.

## Verification and current limits

The built Release executable was run once for this record:

```text
typed_payload_tests: 362 codec and trusted-boundary checks passed
```

It exited with code `0`. The [test source](../tests/typed_payload_tests.cpp) covers independent golden byte sequences, envelope and payload truncation, maximum-size roster truncation, extra bytes, unknown versions/kinds/schemas, NaNs and infinities, numeric bounds, unknown button bits, invalid and duplicate slots, zero generations, stale binding and slot generations, cross-peer input claims, unauthorized pose senders, incomplete rosters, and complete updates under router reordering. The tests exercise the real portable router and session coordinator where those boundaries are relevant.

The new [receiver tests](../tests/input_receiver_tests.cpp) pass **783 host admission and rollback checks** in Release. They cover rejected high sequences followed by valid lower sequences, malformed and unauthorized input, recycled bindings/slots, Session clock/phase/replay rejection, sequence wrap, held-input expiry, four independent sources and preservation of queued reliable events. All 11 shared CTest suites passed in the [coordinated build](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-tests-with-input-receiver-2026-09-09.log); the 362-check result above belongs only to the codec suite. An independent code review found no blocker under the trusted-adapter and exclusive-ownership contract. Allocation-failure rollback was established by code inspection, without injected allocation failures or a throughput benchmark.

These checks establish codec and coordinator behavior in a local test executable. They do not demonstrate a Steam multiplayer session, authenticated remote delivery, an in-game input adapter, remote actor allocation, replicated AI, mission synchronization, save isolation, or start-to-finish co-op. No game process is read, written, or controlled by the codec tests.
