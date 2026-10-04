#pragma once
// Machine-level Steam ABI constants and POD layouts for the co-op lab shim.
//
// Interface versions come from the strings in the shipped steam_api64.dll
// (SHA-256 08bd0152...): SteamUser018, SteamFriends015,
// STEAMREMOTESTORAGE_INTERFACE_VERSION012, SteamNetworking005. Slot indices are
// the declaration order of those exact SDK headers (none of the four declares
// overloaded virtuals, so MSVC keeps declaration order), cross-checked against
// the game's own call sites (tools/generate_steam_shim_contract.py). No Valve
// class is reproduced; only offsets and indices that cross the x64 boundary.
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef MGSCOOP_STEAM_CALL
#define MGSCOOP_STEAM_CALL __cdecl
#endif
#else
#ifndef MGSCOOP_STEAM_CALL
#define MGSCOOP_STEAM_CALL
#endif
#endif

namespace mgscoop::steam {

// ---- Callback identities (transport ctor 0x1A5F470: mov edx,4B2h / 4B3h). ----
constexpr int kCallbackP2PSessionRequest = 1202;
constexpr int kCallbackP2PSessionConnectFail = 1203;

// ---- EP2PSessionError ----
constexpr std::uint8_t kP2PErrorNone = 0;
constexpr std::uint8_t kP2PErrorTimeout = 4;

// ---- EP2PSend. The game sends mode 0 (Unreliable) on channel 0. ----
constexpr std::int32_t kP2PSendUnreliable = 0;
constexpr std::int32_t kP2PSendUnreliableNoDelay = 1;
constexpr std::int32_t kP2PSendReliable = 2;
constexpr std::int32_t kP2PSendReliableWithBuffering = 3;
inline bool is_reliable_mode(std::int32_t mode) noexcept {
    return mode == kP2PSendReliable || mode == kP2PSendReliableWithBuffering;
}
inline bool is_valid_mode(std::int32_t mode) noexcept { return mode >= 0 && mode <= 3; }

// Valve: unreliable sends are limited to 1200 bytes, reliable to 1 MiB.
constexpr std::uint32_t kMaxUnreliablePayload = 1200;
constexpr std::uint32_t kMaxReliablePayload = 1024u * 1024u;

// ---- Callback payloads. CSteamID is declared under pack(1) in the SDK, so the
// game's GetCallbackSizeBytes thunks return 8 (0x1A5F830) and 9 (0x1D38940). ----
#pragma pack(push, 1)
struct P2PSessionRequest_t {
    std::uint64_t steam_id_remote;
};
struct P2PSessionConnectFail_t {
    std::uint64_t steam_id_remote;
    std::uint8_t p2p_session_error;
};
#pragma pack(pop)
static_assert(sizeof(P2PSessionRequest_t) == 8, "1202 payload is 8 bytes");
static_assert(sizeof(P2PSessionConnectFail_t) == 9, "1203 payload is 9 bytes");

// ---- P2PSessionState_t. The game (0x1A5F8C0) passes a stack buffer to slot 6 and
// reads the remote IP at +0x0C and the port at +0x10 after a true return. ----
struct P2PSessionState_t {
    std::uint8_t connection_active;
    std::uint8_t connecting;
    std::uint8_t p2p_session_error;
    std::uint8_t using_relay;
    std::int32_t bytes_queued_for_send;
    std::int32_t packets_queued_for_send;
    std::uint32_t remote_ip;  // host byte order: 0x7F000001 == 127.0.0.1
    std::uint16_t remote_port;
};
static_assert(sizeof(P2PSessionState_t) == 20, "P2PSessionState_t size");
static_assert(offsetof(P2PSessionState_t, remote_ip) == 0x0C, "remote IP offset read by the game");
static_assert(offsetof(P2PSessionState_t, remote_port) == 0x10, "remote port offset read by the game");

// ---- CCallbackBase (MSVC x64). steam_api64 RegisterCallback 0x2B10:
// or byte [cb+8],1 ; mov [cb+0Ch],id. UnregisterCallback 0x3670 clears bit 0. ----
constexpr std::size_t kCallbackFlagsOffset = 0x08;  // uint8 m_nCallbackFlags
constexpr std::size_t kCallbackIdOffset = 0x0C;     // int32 m_iCallback
constexpr std::uint8_t kCallbackFlagRegistered = 0x01;
constexpr std::uint8_t kCallbackFlagGameServer = 0x02;
// MSVC places the overloads Run(void*) / Run(void*, bool, SteamAPICall_t) in
// reverse declaration order: slot 0 = Run(p, bIOFailure, hCall), slot 1 =
// Run(p), slot 2 = GetCallbackSizeBytes. steam_api64's own dispatch loop calls
// [vtable+8] (RVA 0x2DF7) with rcx=cb, rdx=payload, so the lab uses slot 1.
constexpr std::size_t kCallbackRunSlot = 1;
constexpr std::size_t kCallbackSizeSlot = 2;

namespace net_slot {
constexpr unsigned SendP2PPacket = 0;
constexpr unsigned IsP2PPacketAvailable = 1;
constexpr unsigned ReadP2PPacket = 2;
constexpr unsigned AcceptP2PSessionWithUser = 3;
constexpr unsigned CloseP2PSessionWithUser = 4;
constexpr unsigned CloseP2PChannelWithUser = 5;
constexpr unsigned GetP2PSessionState = 6;
constexpr unsigned AllowP2PPacketRelay = 7;
constexpr unsigned kCount = 22;  // SteamNetworking005 slots 0..21
}  // namespace net_slot

namespace user_slot {
constexpr unsigned GetHSteamUser = 0;
constexpr unsigned BLoggedOn = 1;
constexpr unsigned GetSteamID = 2;           // CSteamID by hidden return pointer
constexpr unsigned GetUserDataFolder = 6;
constexpr unsigned GetAuthSessionTicket = 13;
constexpr unsigned kCount = 25;  // SteamUser018 slots 0..24
}  // namespace user_slot

namespace friends_slot {
constexpr unsigned GetPersonaName = 0;
constexpr unsigned GetFriendPersonaName = 7;
constexpr unsigned ActivateGameOverlayToUser = 29;
constexpr unsigned RequestUserInformation = 37;
constexpr unsigned kCount = 72;  // SteamFriends015 slots 0..71
}  // namespace friends_slot

namespace storage_slot {
constexpr unsigned FileWrite = 0;
constexpr unsigned FileRead = 1;
constexpr unsigned FileForget = 2;
constexpr unsigned FileDelete = 3;
constexpr unsigned FileShare = 4;
constexpr unsigned SetSyncPlatforms = 5;
constexpr unsigned FileWriteStreamOpen = 6;
constexpr unsigned FileWriteStreamWriteChunk = 7;
constexpr unsigned FileWriteStreamClose = 8;
constexpr unsigned FileWriteStreamCancel = 9;
constexpr unsigned FileExists = 10;
constexpr unsigned FilePersisted = 11;
constexpr unsigned GetFileSize = 12;
constexpr unsigned GetFileTimestamp = 13;
constexpr unsigned GetFileCount = 15;
constexpr unsigned GetFileNameAndSize = 16;
constexpr unsigned kCount = 57;  // RemoteStorage012 slots 0..56
}  // namespace storage_slot

// Individual-account SteamID64 in the public universe: 0x0110000100000000 | account.
inline bool is_individual_steamid(std::uint64_t id) noexcept {
    return (id >> 32) == 0x01100001u && (id & 0xffffffffu) != 0;
}

}  // namespace mgscoop::steam
