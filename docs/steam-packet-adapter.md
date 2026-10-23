# Original Steam packet adapter

2026-09-09. **Implemented post-admission packet I/O against the shipped legacy Steam flat-export boundary, with 124 passing fixture checks. It is not connected to the running game.** The installed loader remains unchanged. No Steam packet was sent, received or accepted through a real Steam session in this work.

Source: [public contracts](../native/include/mgscoop/steam_packets.hpp), [implementation](../native/steam_packets.cpp), [tests](../tests/steam_packet_tests.cpp). No Dynamite source or dependency is used.

## Independently checked boundary

The inspected `steam_api64.dll` is 211,368 bytes, AMD64, SHA-256 `08bd0152356e5619ee56a6714ac951b6800dbb21821693a4efaf85278442c60a`. The original [static checker](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/tools/inspect_steam_packet_abi.py) verifies the whole fingerprint, export identities, and complete small wrapper bytes without loading the DLL. [Recorded result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/steam-packet-abi-2026-09-09.json).

| Export suffix after `SteamAPI_ISteamNetworking_` | RVA | Independently decoded wrapper |
| --- | --- | --- |
| `SendP2PPacket` | `0x1D10` | 31 bytes; preserves the first four register arguments, copies the two trailing 32-bit stack arguments, calls interface slot 0, returns the original Boolean. |
| `IsP2PPacketAvailable` | `0x1950` | 7 bytes; loads the instance table and tail-calls slot `+8` with unchanged arguments. |
| `ReadP2PPacket` | `0x21A0` | 7 bytes; loads the instance table and tail-calls slot `+0x10` with unchanged arguments. |

Valve's historical [flat declarations](https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_133/steam_api_flat.h), [networking interface](https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_133/isteamnetworking.h), and [Steam ID definition](https://github.com/ValveSoftware/Proton/blob/proton_11.0/lsteamclient/steamworks_sdk_133/steamclientpublic.h) were read directly from Valve's repository. They establish an integer-sized instance pointer, an eight-byte Steam ID value/output, Boolean results and the argument ordering. The interface header names `SteamNetworking005`. This does not identify the entire shipped SDK as version 1.33.

The adapter declares Windows x64 machine-level function pointer types, representing Steam ID bits with `uint64_t`; it does not reproduce Valve's C++ class. The first four parameters use register positions, and remaining parameters use stack slots. Eight-byte aggregates use the corresponding integer argument convention. [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170). The fake-export tests exercise argument widths/order in the Windows x64 build; they do not call the actual DLL. Other platform ABIs are not established.

## Borrowed access and ownership

`SteamPacketChannel` is a synchronous borrowed context. Every operation requires independently established initialization/serialization, a compatible live interface, a held compatible export module, and exclusive ownership of a channel proven not to conflict with MGSV. These are trusted adapter obligations, not facts produced by setting flags. The caller must prevent overlapping shutdown, unloading, peer changes and other channel readers for the entire call. No persistent pointer cache, lock or lifecycle observer is supplied here.

There is no default channel. Zero and negative channel numbers are rejected, and a positive number alone proves nothing about ownership. Channel 17 exists only in the tests and is **not** a proposed live channel assignment. The existing live initialization/lifecycle gates remain closed.

The resolver requests exactly three export names, returning an empty table if any is missing. Unknown lifecycle or ABI produces no resolver calls. It never loads a library or executes an export. Packet operations likewise make no SDK calls when access is gated, invalid or incomplete.

## Sending

The recipient must have an exact current `PeerRouter::PeerHandle`; disconnected or recycled handles fail before the SDK call. This is post-admission data I/O. Pending authentication and the initial control handshake need a separate bounded path; this module cannot bootstrap them by pretending a peer is already authenticated.

Payloads are bounded to 1–1,200 bytes. Replaceable state maps to Valve mode 1, which drops data while the underlying connection is unavailable. Reliable traffic maps to mode 2. A true SDK result becomes `ApiAccepted`, not an acknowledgement of receipt or a committed gameplay event. Sending can implicitly accept the underlying P2P session. [Valve packet API and delivery modes](https://partner.steamgames.com/doc/api/ISteamNetworking).

No retries, fragmentation, event IDs, authentication calls, callback registration, relay settings or connection closes are added. The owner decides whether to replace lost movement, retry a control message or stop the connection after an error.

## Receiving

One polling call checks availability once and reads at most once into a fixed 1,200-byte buffer. It contains no allocation or internal draining loop. No other channel is inspected. A queued zero-length message is consumed and rejected. Read failure, missing sender identity, inconsistent reported length, or out-of-range length exposes no datagram.

Steam provides the received sender ID, and an undersized receive buffer can truncate a message. [Valve receive contract](https://partner.steamgames.com/doc/api/ISteamNetworking#ReadP2PPacket). An advertised size above the local cap therefore returns `OversizedHead` without reading. The channel owner must suspend or quarantine that channel; repeatedly polling the same head would make no progress. This component does not close a shared P2P session or discard an unknown number of messages to recover.

Successful reads return `ReceivedUnadmitted`: raw bytes and the SDK-reported sender ID. A current Steam ID lookup must not silently assign a new connection generation to a packet queued before reconnection. The future authenticated handshake must establish a replay boundary before passing a trusted binding/session to `receive_host_input`. The packet body cannot supply its own authority. No automatic raw-datagram-to-Session dispatch exists.

## Verification and remaining work

All **12 CTest suites passed** after fixing the test fixture's exception warning; the new target builds with warnings treated as errors. [Build result](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-tests-with-steam-packets-2026-09-09.log), [individual results](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/native-test-details-with-steam-packets-2026-09-09.log).

The 124 packet checks cover gated zero-call behavior, missing exports, stale recipient handles, 64-bit identities, delivery modes, maximum-size bytes, empty/failed/inconsistent reads, oversized heads, unrelated-channel preservation and a bounded three-guest input path into the actual portable coordinator. That path uses fake exports and explicit fixture admission, not real accounts or network connections. The static checker also [refused an unrelated fixture DLL](https://github.com/TrollTaylor/diamond-dogs-together/blob/archive/research-2026-10-23/docs/evidence/steam-packet-abi-rejection-2026-09-09.json).

Next live requirements are unchanged: observe successful natural Steam startup and its lifecycle ownership, obtain the compatible interface under a verified lifetime, prove non-conflicting channel and callback ownership, establish authenticated admission/replay protection, and exchange data between two legitimate installations. Creating and synchronizing additional campaign actors remains separate required engine work. None of these checks establishes playable co-op or campaign support.
