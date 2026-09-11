# Steam and native networking integration

Research snapshot: 2026-09-09. This is a design and source audit, not a successful multiplayer runtime test.

## Recommended path

Build an original adapter around the installed game's Steam context and independently mapped Fox Engine interfaces. Keep Steam identity, party admission, packet routing, and campaign synchronization explicit. Add Steam friends/lobbies as a discovery and invitation layer only after confirming that a separate mod lobby can coexist with the game's own session callbacks. Existing native multiplayer functionality is a candidate for reuse only after independent inspection establishes its semantics and player capacity.

Keep the portable campaign/session core independent of Steam and game addresses. A successful standalone test proves its policy and packet handling; it does not prove that Fox can instantiate or replicate four campaign actors.

## Locally observed installation

The following were inspected as bytes without loading DLLs or changing the installation:

| Item | Observation |
| --- | --- |
| DLL | `D:\Games\steamapps\common\MGS_TPP\steam_api64.dll` |
| Size / architecture | 211,368 bytes; PE machine `0x8664` (x64) |
| SHA-256 | `08bd0152356e5619ee56a6714ac951b6800dbb21821693a4efaf85278442c60a` |
| Named exports | 747 |
| Existing `steam_appid.txt` | `287700` |
| DLL interface strings | `SteamClient017`, `SteamUser018`, `SteamFriends015`, `SteamMatchMaking009`, `SteamNetworking005`, `STEAMAPPS_INTERFACE_VERSION007` |
| Strings in both game executables | `SteamClient015`, `SteamUser017`, `STEAMAPPS_INTERFACE_VERSION006` |
| Modern exported interfaces | No names containing `NetworkingSockets`, `NetworkingMessages`, or `ManualDispatch` |

Relevant named exports actually present include:

```text
SteamAPI_Init / SteamAPI_RunCallbacks / SteamAPI_RegisterCallback
SteamClient / SteamNetworking / SteamMatchmaking / SteamUser
SteamAPI_ISteamClient_GetISteamGenericInterface
SteamAPI_ISteamMatchmaking_CreateLobby / JoinLobby / LeaveLobby
SteamAPI_ISteamMatchmaking_GetLobbyOwner / SetLobbyData / SetLobbyMemberLimit
SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser / AllowP2PPacketRelay
SteamAPI_ISteamNetworking_IsP2PPacketAvailable / ReadP2PPacket / SendP2PPacket
SteamAPI_ISteamNetworking_GetP2PSessionState
SteamAPI_ISteamNetworking_CloseP2PChannelWithUser / CloseP2PSessionWithUser
SteamAPI_ISteamUser_GetAuthSessionTicket / BeginAuthSession
SteamAPI_ISteamUser_CancelAuthTicket / EndAuthSession / UserHasLicenseForApp
```

The grouped suffix notation above abbreviates exact export names with the same prefix. Exports and embedded strings establish a compatibility surface, not successful initialization, current AppID permissions, actual interfaces requested at runtime, or four-player support. Missing modern DLL exports do not prove that no current Steam client interface could ever be obtained through a correctly versioned generic interface; that remains an untested capability.

## Original engine adapter boundary

No engine address map, packet format, player limit, replication hook, or callback implementation from another mod forms part of this design. The currently verified native evidence is limited to the installation's PE exports and interface strings above. A separate independent investigation must identify the actual game lifecycle, session membership representation, actor creation, and native replication behavior.

The original `PeerRouter` prerequisite stores owned message bytes, accepts explicit engine-validated peer bindings, assigns non-recycled connection generations, and maintains separate state/event sequence handling per peer. Reordered events wait in a bounded queue; snapshots may supersede older snapshots. This policy makes no claim about how the game's existing networking code behaves.

## Steam feature boundaries

### Friends, lobbies, and invitations

Use a private or friends-only lobby with capacity four. Proposed metadata: mod identifier, protocol version, executable fingerprint, content manifest hash, campaign identifier, mission/phase, and joinability. The host validates all of these again during the transport handshake; metadata is discovery information.

Steam supports lobby creation/joining, member enumeration, metadata, and invitation flows. Only the lobby owner writes lobby-wide metadata. A lobby transports coordination data, not the campaign simulation. [Valve matchmaking overview](https://partner.steamgames.com/doc/features/multiplayer/matchmaking)

Steam automatically chooses another lobby owner when the owner leaves. That does not migrate Fox simulation authority or the campaign save. Initial mod policy should stop at a safe checkpoint when the campaign host disconnects, even if the Steam lobby survives. `SetLobbyJoinable(false)` can close admission during a transition. Creation can fail when an AppID does not allow lobbies or the user lacks access; verify this on legitimate MGSV accounts before promising the feature. [ISteamMatchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking)

### Packet transport and relay

Valve recommends `ISteamNetworkingSockets` or `ISteamNetworkingMessages` for new integrations; its old `ISteamNetworking` interface is deprecated. Modern networking can use Valve's relay network. This is a future adapter option, not evidence that the installed MGSV DLL supports those APIs. [Steam networking overview](https://partner.steamgames.com/doc/features/multiplayer/networking)

The legacy API supports P2P admission, sending, receiving, channel selection, connection state, and relay fallback. Relay fallback is permitted by default. `ReadP2PPacket` supplies the sender's SteamID; retain that identity through the native bridge. A raw packet's claimed sender field must never replace transport identity. A separate custom channel must be proven not to collide with native channels, and its receive loop must not consume the game's packets. [ISteamNetworking](https://partner.steamgames.com/doc/api/ISteamNetworking)

If a modern adapter becomes viable, an explicit host listener plus three client connections maps naturally to `CreateListenSocketP2P`/`ConnectP2P`. Keep processing connection callbacks during loading and promptly accept or reject requests. [ISteamNetworkingSockets](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets)

Valve's open-source [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets) is a usable transport library, but adding it alone does not provide Steam lobbies, Steam identities, or access to SDR services. Dedicated-server relay tickets and certificate issuance involve separate service setup; they are unnecessary for the first host-and-three-guests milestone. [SDR documentation](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay)

### Authentication, ownership, and lifecycle

Use the already initialized game Steam context. The existing AppID text is observation only; do not overwrite it or substitute a sample AppID. Initialization depends on the Steam client, matching OS context, a valid application context, and a license. The injected module should register only its own callbacks and leave process initialization, callback pumping, and shutdown with the owner unless integration testing establishes another contract. Do not blindly compile current SDK virtual interfaces against older returned objects. [Steamworks API overview](https://partner.steamgames.com/doc/sdk/api)

Where the engine already authenticates an admitted peer, carry that verified identity into the mod. Otherwise use a version-compatible session-ticket path: issue a fresh ticket for each verifier, pass it to `BeginAuthSession`, and handle asynchronous validation and later invalidation. Ticket validation verifies ownership of the current AppID. The mod must distinguish pending from verified identity; an immediate API return is not the final result. End sessions and cancel tickets on disconnect. Do not duplicate an engine-owned authentication session without checking its lifecycle. Publisher Web API keys and encrypted-ticket private keys are not needed in a peer mod and must not be embedded in it. [Valve authentication and ownership](https://partner.steamgames.com/doc/features/auth)

## Adapter contract and proof milestones

The adapter supplies the core with authenticated peer bindings, stable peer generations, connection/disconnection events, and received bytes with their actual source. The core handles at most four players including the host, separate per-peer sequence tracking, bounded queues, protocol/content compatibility, mission authority, and checkpoint barriers. No native pointers cross into portable packet structures.

Use distinct delivery policies: reliable ordered events for mission decisions and interactions; latest-state messages for replaceable movement snapshots; bounded acknowledged chunks for checkpoint state. The first native adapter must report the reliability semantics it actually provides. Never label raw native send as reliable without measuring or proving it.

Required evidence before calling integration complete:

1. Pin an executable fingerprint and confirm its addresses and Steam interfaces in a running legitimate installation.
2. Establish an original two-player transport handshake and reconnection with the unmodified Steam DLL.
3. Preserve native synchronization while carrying an authenticated custom message in both directions.
4. Demonstrate three distinct remote peer identities concurrently, with independent ordering, disconnect cleanup, and no native packet starvation.
5. Verify four independently controllable campaign actors across a mission transition; increasing lobby capacity alone does not satisfy this.
6. Verify invitations, full-party rejection, incompatible versions, restrictive NAT/relay, Steam outages, host loss, and save recovery across four separate accounts and processes.

No game was launched, no Steam session was initialized, no lobby was created, no live relay or ownership behavior was tested, and no proprietary SDK was downloaded during this research.
