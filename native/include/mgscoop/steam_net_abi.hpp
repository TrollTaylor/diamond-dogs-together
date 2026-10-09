#pragma once
// Machine-level ABI of the Steam networking interfaces the online transport
// uses (docs/steam-online.md section 4). Pure: offsets, ids, slot indices and
// abstract interface classes whose declaration order is the exact vtable order
// of the running steamclient64 adapters (verified by RTTI, forwarder shape and
// name strings; tools/generate_steam_shim_contract.py --steamclient). No Valve
// class is reproduced; only what crosses the x64 boundary.
//
// The interface classes have no overloads and a protected non-virtual
// destructor, so MSVC emits `call [vptr+8N]` for slot N. Unused slots are
// declared as placeholders (never called) so every index is exact; the fixture
// tests call each used slot through its raw vtable index.
#include <cstddef>
#include <cstdint>
#include "mgscoop/steam_abi.hpp"

namespace mgscoop::steam_net {

// ---- Interface versions and the RTTI names of their steamclient64 adapters.
inline constexpr char kMessagesVersion[] = "SteamNetworkingMessages002";
inline constexpr char kSocketsVersion[] = "SteamNetworkingSockets012";
inline constexpr char kUtilsVersion[] = "SteamNetworkingUtils004";
inline constexpr char kRttiMessages[] = ".?AVCAdapterSteamNetworkingMessages002@@";
inline constexpr char kRttiSockets[] = ".?AVCAdapterSteamNetworkingSockets012@@";
inline constexpr char kRttiUtils[] = ".?AVCAdapterSteamNetworkingUtils004@@";
inline constexpr char kRttiClient[] = ".?AVCAdapterSteamClient017@@";
// The inner objects the Sockets012 / Utils004 adapters forward to (adapter +8),
// and the Messages implementation whose IClientNetworkingMessages subobject the
// Messages002 adapter forwards to (docs/steam-online.md section 20).
inline constexpr char kRttiSocketsInner[] = ".?AVCSteamNetworkingSocketsSteamClient@SteamNetworkingSocketsLib@@";
inline constexpr char kRttiUtilsInner[] = ".?AVCSteamNetworkingUtilsSteamClient@SteamNetworkingSocketsLib@@";
inline constexpr char kRttiMessagesImpl[] = ".?AVCSteamNetworkingMessages@SteamNetworkingSocketsLib@@";
// CSteamNetworkingMessages: the IClientNetworkingMessages subobject is at
// this+0xD0 (its COL offset), and this+8 is the owning sockets object
// (BHandleNewIncomingConnection: mov rcx,[this+8]; call [r10+0x220]).
inline constexpr std::uint32_t kMessagesSubobjectOffset = 0xD0;
inline constexpr std::uint32_t kMessagesOwnerSockets = 0x08;
// Slot counts of the adapters (RTTI census): Messages002 has exactly 6.
inline constexpr unsigned kMessagesSlots = 6;
inline constexpr unsigned kSocketsSlots = 47;
inline constexpr unsigned kUtilsSlots = 25;
inline constexpr unsigned kClientSlots = 37;
// ISteamClient017::GetISteamGenericInterface (flat export tail-jumps to [vt+60h]).
inline constexpr unsigned kClientGenericInterfaceSlot = 12;

// ---- SteamNetworkingIdentity (#pragma pack(1), 136 bytes).
inline constexpr std::int32_t kIdentityInvalid = 0;
inline constexpr std::int32_t kIdentityIp = 1;
inline constexpr std::int32_t kIdentitySteamId = 16;
inline constexpr std::int32_t kIdentitySteamIdSize = 8;
inline constexpr std::int32_t kIdentityIpSize = 0x12;  // SteamNetworkingIPAddr
#pragma pack(push, 1)
struct Identity {
    std::int32_t type;
    std::int32_t size;
    std::uint8_t data[128];
};
#pragma pack(pop)
static_assert(sizeof(Identity) == 136, "SteamNetworkingIdentity is 136 bytes");
static_assert(offsetof(Identity, size) == 4 && offsetof(Identity, data) == 8, "identity field offsets");

inline std::uint64_t SteamIdFromAccount(std::uint32_t account) noexcept {
    return 0x0110000100000000ull | account;
}
inline std::uint32_t AccountOf(std::uint64_t id) noexcept { return static_cast<std::uint32_t>(id & 0xffffffffu); }
// Zero-fills the whole identity, then type 16, size 8 and the SteamID64.
inline void MakeSteamIdentity(std::uint64_t id, Identity& out) noexcept {
    auto* bytes = reinterpret_cast<std::uint8_t*>(&out);
    for (std::size_t i = 0; i < sizeof(Identity); ++i) bytes[i] = 0;
    out.type = kIdentitySteamId;
    out.size = kIdentitySteamIdSize;
    for (unsigned i = 0; i < 8; ++i) out.data[i] = static_cast<std::uint8_t>(id >> (8 * i));
}
// True for type 16, size 8 and an individual public-universe SteamID.
inline bool IdentitySteamId(const Identity& identity, std::uint64_t& out) noexcept {
    out = 0;
    if (identity.type != kIdentitySteamId || identity.size != kIdentitySteamIdSize) return false;
    std::uint64_t id = 0;
    for (unsigned i = 0; i < 8; ++i) id |= static_cast<std::uint64_t>(identity.data[i]) << (8 * i);
    if (!steam::is_individual_steamid(id)) return false;
    out = id;
    return true;
}
// A socket pair created with null identities carries IP-localhost identities.
inline bool IdentityIsIp(const Identity& identity) noexcept {
    return identity.type == kIdentityIp && identity.size == kIdentityIpSize;
}

// ---- SteamNetworkingMessage_t (AllocateMessage 0xAA2CD0 stores; 0x108 bytes).
inline constexpr std::size_t kMsgData = 0x00;       // void* m_pData
inline constexpr std::size_t kMsgSize = 0x08;       // int m_cbSize
inline constexpr std::size_t kMsgConn = 0x0C;       // HSteamNetConnection m_conn
inline constexpr std::size_t kMsgIdentity = 0x10;   // SteamNetworkingIdentity m_identityPeer
inline constexpr std::size_t kMsgFreeData = 0xB0;   // m_pfnFreeData
inline constexpr std::size_t kMsgRelease = 0xB8;    // m_pfnRelease
inline constexpr std::size_t kMsgChannel = 0xC0;    // int m_nChannel (-1 until set)
inline constexpr std::size_t kMsgLane = 0xD0;       // uint16 m_idxLane
inline constexpr std::size_t kMsgObjectSize = 0x108;
static_assert(kMsgIdentity + sizeof(Identity) <= kMsgFreeData, "identity ends before the free-data pointer");
using MessageReleaseFn = void (*)(void* message);

struct MessageView {
    const std::uint8_t* data;
    std::int32_t size;
    std::uint32_t conn;
    const Identity* identity;
    std::int32_t channel;
};
// Reads the public prefix of a received message (no call into steamclient).
inline MessageView ReadMessage(const void* message) noexcept {
    const auto* m = static_cast<const std::uint8_t*>(message);
    MessageView v{};
    v.data = *reinterpret_cast<const std::uint8_t* const*>(m + kMsgData);
    v.size = *reinterpret_cast<const std::int32_t*>(m + kMsgSize);
    v.conn = *reinterpret_cast<const std::uint32_t*>(m + kMsgConn);
    v.identity = reinterpret_cast<const Identity*>(m + kMsgIdentity);
    v.channel = *reinterpret_cast<const std::int32_t*>(m + kMsgChannel);
    return v;
}
inline MessageReleaseFn MessageRelease(const void* message) noexcept {
    return *reinterpret_cast<const MessageReleaseFn*>(static_cast<const std::uint8_t*>(message) + kMsgRelease);
}

// ---- SteamNetConnectionInfo_t (GetSessionConnectionInfo memsets 0x2B8 bytes).
inline constexpr std::size_t kConnInfoSize = 0x2B8;
inline constexpr std::size_t kConnInfoIdentity = 0x00;
inline constexpr std::size_t kConnInfoRelayPop = 0xAC;
inline constexpr std::size_t kConnInfoState = 0xB0;  // == the return value (verified)
inline constexpr std::size_t kConnInfoFlags = 0x1B8;  // SDK layout; used only after the cross-check
inline constexpr std::int32_t kConnFlagRelayed = 16;
// End reason and debug text (connection-info filler 0xA9F7C0 in caba4826...:
// mov [info+0xB4],eax; copy 0x80 bytes to info+0xB8).
inline constexpr std::size_t kConnInfoEndReason = 0xB4;
inline constexpr std::size_t kConnInfoEndDebug = 0xB8;
inline constexpr std::size_t kConnInfoEndDebugSize = 0x80;
// The quick status the API would fill (0x78 bytes); always passed as null.
inline constexpr std::size_t kQuickStatusSize = 0x78;
enum ConnectionState : std::int32_t {
    kConnNone = 0, kConnConnecting = 1, kConnFindingRoute = 2, kConnConnected = 3, kConnClosedByPeer = 4,
    kConnProblemDetectedLocally = 5
};
inline const char* ConnectionStateName(std::int32_t state) noexcept {
    switch (state) {
    case kConnNone: return "none";
    case kConnConnecting: return "connecting";
    case kConnFindingRoute: return "finding_route";
    case kConnConnected: return "connected";
    case kConnClosedByPeer: return "closed_by_peer";
    case kConnProblemDetectedLocally: return "problem_detected_locally";
    default: return "unknown";
    }
}
enum class Route : std::uint8_t { none, unknown, direct, relay };
inline const char* RouteName(Route route) noexcept {
    switch (route) {
    case Route::unknown: return "unknown";
    case Route::direct: return "direct";
    case Route::relay: return "relay";
    default: return "none";
    }
}
struct RouteInfo {
    Route route;
    std::int32_t state;
};
// Decodes a GetSessionConnectionInfo result: valid only when the returned
// state equals info+0xB0 and the identity at info+0 is `peer`; then the
// Relayed flag decides relay/direct. Anything else is route=unknown.
inline RouteInfo DecodeConnectionInfo(std::int32_t returned, const std::uint8_t* info, std::uint64_t peer) noexcept {
    RouteInfo out{Route::unknown, returned};
    if (!info) return out;
    const std::int32_t state = *reinterpret_cast<const std::int32_t*>(info + kConnInfoState);
    std::uint64_t id = 0;
    if (state != returned || !IdentitySteamId(*reinterpret_cast<const Identity*>(info + kConnInfoIdentity), id) || id != peer)
        return out;
    if (state != kConnConnected) return out;
    const std::int32_t flags = *reinterpret_cast<const std::int32_t*>(info + kConnInfoFlags);
    out.route = (flags & kConnFlagRelayed) ? Route::relay : Route::direct;
    return out;
}

// ---- EResult values the transport counts.
inline constexpr std::int32_t kResultOk = 1;
inline constexpr std::int32_t kResultNoConnection = 3;
inline constexpr std::int32_t kResultInvalidParam = 8;
inline constexpr std::int32_t kResultLimitExceeded = 25;

// ---- Send flags. AutoRestartBrokenSession is Messages-only.
inline constexpr std::int32_t kSendUnreliableNoNagle = 0x01;
inline constexpr std::int32_t kSendAutoRestartBrokenSession = 0x20;
inline constexpr std::int32_t kMsgFlags = kSendUnreliableNoNagle | kSendAutoRestartBrokenSession;  // 0x21
inline constexpr std::int32_t kConnFlags = kSendUnreliableNoNagle;                                  // 1

// ---- Link constants on Steam.
inline constexpr std::int32_t kLinkChannel = 0x4D47;   // never 0 (the game's legacy P2P uses channel 0)
inline constexpr std::uint16_t kSteamPortTag = 0x5354;  // Endpoint{account, 0x5354}
inline constexpr std::size_t kMaxMessage = 1200;        // link v2's datagram cap
inline constexpr std::uint32_t kAppId = 287700;

// ---- Callbacks (posted through the game's pipe; we register only 1251 / 337).
inline constexpr int kCallbackSessionRequest = 1251;  // SteamNetworkingMessagesSessionRequest_t
inline constexpr int kCallbackSessionFailed = 1252;   // never registered; function pointer 205 only
inline constexpr int kCallbackJoinRequested = 337;    // GameRichPresenceJoinRequested_t
inline constexpr int kSessionRequestSize = 136;
inline constexpr std::size_t kConnectStringMax = 256;  // k_cchMaxRichPresenceValueLength
struct JoinRequested {
    std::uint64_t friend_id;
    char connect[kConnectStringMax];
};
static_assert(sizeof(JoinRequested) == 264 && offsetof(JoinRequested, connect) == 8, "337 payload layout");
inline constexpr int kJoinRequestedSize = 264;
// The game registers exactly these ids (generator census); 1251/337 are ours.
inline constexpr int kGameCallbackIds[] = {152, 163, 331, 333, 1101, 1102, 1103, 1202, 1203};

// ---- Function-pointer dispatch (docs/steam-online.md section 20). A config
// value of type Ptr names a function steamclient queues the callback for;
// ISteamNetworkingSockets012::RunCallbacks() then calls it with rcx = the
// payload, on the calling thread, with no Steam networking lock held.
// Steamclient still posts the same callback to the game's pipe (unchanged).
inline constexpr std::int32_t kConfigConnectionStatusChanged = 201;  // Connection / ListenSocket scope
inline constexpr std::int32_t kConfigMessagesSessionRequest = 204;   // Global only
inline constexpr std::int32_t kConfigMessagesSessionFailed = 205;    // Global only
inline constexpr std::int32_t kConfigScopeGlobal = 1;
inline constexpr std::int32_t kConfigScopeSocketsInterface = 2;
inline constexpr std::int32_t kConfigScopeListenSocket = 3;
inline constexpr std::int32_t kConfigScopeConnection = 4;
inline constexpr std::int32_t kConfigTypePtr = 5;
using ConfigCallbackFn = void (*)(void* payload);
// SteamNetConnectionStatusChangedCallback_t (1221): hConn, info, old state.
inline constexpr int kCallbackConnectionStatusChanged = 1221;
inline constexpr std::size_t kStatusChangedSize = 0x2C8;
inline constexpr std::size_t kStatusChangedConn = 0x00;
inline constexpr std::size_t kStatusChangedInfo = 0x08;
inline constexpr std::size_t kStatusChangedOldState = 0x2C0;
// SteamNetworkingMessagesSessionFailed_t (1252): the connection info only.
inline constexpr std::size_t kSessionFailedSize = 0x2B8;
static_assert(kStatusChangedInfo + kConnInfoSize <= kStatusChangedOldState, "1221 payload: info before the old state");
static_assert(kSessionFailedSize == kConnInfoSize, "1252 payload is SteamNetConnectionInfo_t");
// The ids RunCallbacks dispatches (its jump table; anything else asserts).
inline constexpr int kRunCallbacksIds[] = {1221, 1222, 1223, 1251, 1252, 1281};

// ---- Hand-built CCallbackBase (no C++ virtuals): {vptr; u8 flags; pad; i32 id}.
// steam_api64 RegisterCallback 0x2B10 ORs bit 0 into +8 and stores the id at
// +0xC; its dispatch (0x2DE0..0x2DF9) compares the GameServer bit and calls
// [vt+8] = Run(self, payload). It never calls GetCallbackSizeBytes.
using CallbackRunIoFn = void (*)(void* self, void* payload, bool io_failure, std::uint64_t call);
using CallbackRunFn = void (*)(void* self, void* payload);
using CallbackSizeFn = int (*)(void* self);
struct CallbackObject {
    const void* const* vptr;
    std::uint8_t flags;
    std::uint8_t pad[3];
    std::int32_t id;
};
static_assert(offsetof(CallbackObject, flags) == steam::kCallbackFlagsOffset, "callback flags at +8");
static_assert(offsetof(CallbackObject, id) == steam::kCallbackIdOffset, "callback id at +0xC");
static_assert(steam::kCallbackRunSlot == 1 && steam::kCallbackSizeSlot == 2, "Run(p) slot 1, size slot 2");

// ---- Interface ABI classes.
class ISteamNetworkingMessages002Abi {
public:
    virtual std::int32_t SendMessageToUser(const Identity& remote, const void* data, std::uint32_t size, std::int32_t flags,
                                           std::int32_t channel) = 0;                                   // 0
    virtual std::int32_t ReceiveMessagesOnChannel(std::int32_t channel, void** out, std::int32_t max) = 0;  // 1
    virtual bool AcceptSessionWithUser(const Identity& remote) = 0;                                    // 2
    virtual bool CloseSessionWithUser(const Identity& remote) = 0;                                     // 3
    virtual bool CloseChannelWithUser(const Identity& remote, std::int32_t channel) = 0;               // 4
    virtual std::int32_t GetSessionConnectionInfo(const Identity& remote, void* info, void* quick) = 0;  // 5

protected:
    ~ISteamNetworkingMessages002Abi() = default;
};

class ISteamNetworkingSockets012Abi {
public:
    virtual void Unused0() {}
    virtual void Unused1() {}
    virtual void Unused2() {}
    virtual void Unused3() {}
    virtual void Unused4() {}
    virtual bool CloseConnection(std::uint32_t conn, std::int32_t reason, const char* debug, bool linger) = 0;  // 5
    virtual void Unused6() {}
    virtual void Unused7() {}
    virtual void Unused8() {}
    virtual void Unused9() {}
    virtual void Unused10() {}
    virtual std::int32_t SendMessageToConnection(std::uint32_t conn, const void* data, std::uint32_t size, std::int32_t flags,
                                                 std::int64_t* message_number) = 0;  // 11
    virtual void Unused12() {}
    virtual void Unused13() {}
    virtual std::int32_t ReceiveMessagesOnConnection(std::uint32_t conn, void** out, std::int32_t max) = 0;  // 14
    virtual void Unused15() {}
    virtual void Unused16() {}
    virtual void Unused17() {}
    virtual void Unused18() {}
    virtual bool CreateSocketPair(std::uint32_t* out1, std::uint32_t* out2, bool network_loopback, const Identity* identity1,
                                  const Identity* identity2) = 0;  // 19
    virtual void Unused20() {}
    virtual void Unused21() {}
    virtual void Unused22() {}
    virtual void Unused23() {}
    virtual void Unused24() {}
    virtual void Unused25() {}
    virtual void Unused26() {}
    virtual void Unused27() {}
    virtual void Unused28() {}
    virtual void Unused29() {}
    virtual void Unused30() {}
    virtual void Unused31() {}
    virtual void Unused32() {}
    virtual void Unused33() {}
    virtual void Unused34() {}
    virtual void Unused35() {}
    virtual void Unused36() {}
    virtual void Unused37() {}
    virtual void Unused38() {}
    virtual void Unused39() {}
    virtual void Unused40() {}
    // Dispatches the callbacks queued for function-pointer config values
    // (1221/1222/1223/1251/1252/1281) on the calling thread (section 20).
    virtual void RunCallbacks() = 0;  // 41

protected:
    ~ISteamNetworkingSockets012Abi() = default;
};
inline constexpr unsigned kSocketsCloseConnection = 5, kSocketsSend = 11, kSocketsReceive = 14, kSocketsCreatePair = 19;
inline constexpr unsigned kSocketsRunCallbacks = 41;
inline constexpr unsigned kSocketsDeclared = 42;

class ISteamNetworkingUtils004Abi {
public:
    virtual void Unused0() {}                                     // AllocateMessage
    virtual std::int32_t GetRelayNetworkStatus(void* details) = 0;  // 1
    virtual void Unused2() {}
    virtual void Unused3() {}
    virtual void Unused4() {}
    virtual void Unused5() {}
    virtual void Unused6() {}
    virtual bool CheckPingDataUpToDate(float max_age_seconds) = 0;  // 7
    virtual void Unused8() {}
    virtual void Unused9() {}
    virtual void Unused10() {}
    virtual void Unused11() {}
    virtual void Unused12() {}
    virtual void Unused13() {}
    virtual void Unused14() {}
    virtual void Unused15() {}
    // SetConfigValue(eValue, eScopeType, scopeObj, eDataType, pArg): edx, r8d,
    // r9, [rsp+28h], [rsp+30h]. A Ptr value stores *(void**)pArg; pArg null
    // resets to the default (null).
    virtual bool SetConfigValue(std::int32_t value, std::int32_t scope, std::intptr_t scope_object, std::int32_t type,
                                const void* arg) = 0;  // 16

protected:
    ~ISteamNetworkingUtils004Abi() = default;
};
inline constexpr unsigned kUtilsRelayStatus = 1, kUtilsCheckPing = 7, kUtilsSetConfigValue = 16, kUtilsDeclared = 17;
// The SDK's inline InitRelayNetworkAccess() is CheckPingDataUpToDate(1e10f).
inline constexpr float kWarmRelayMaxAge = 1e10f;

// ---- Flat steam_api64 exports (hash-pinned; docs/steam-online.md 1.2).
// Each takes the interface object first; CSteamID travels as a uint64 in a
// register (8-byte class by value) and returns as uint64 from the wrapper.
using SteamClientFn = void* (*)();
using GetHSteamFn = std::int32_t (*)();
using GetGenericInterfaceFn = void* (*)(void* client, std::int32_t user, std::int32_t pipe, const char* version);
using BLoggedOnFn = bool (*)(void* user);
using GetSteamIdFn = std::uint64_t (*)(void* user);
using GetFriendCountFn = std::int32_t (*)(void* friends, std::int32_t flags);
using GetFriendByIndexFn = std::uint64_t (*)(void* friends, std::int32_t index, std::int32_t flags);
using GetFriendRelationshipFn = std::int32_t (*)(void* friends, std::uint64_t id);
using GetFriendPersonaNameFn = const char* (*)(void* friends, std::uint64_t id);
using GetFriendGamePlayedFn = bool (*)(void* friends, std::uint64_t id, void* info);
using SetRichPresenceFn = bool (*)(void* friends, const char* key, const char* value);
using GetFriendRichPresenceFn = const char* (*)(void* friends, std::uint64_t id, const char* key);
using RequestFriendRichPresenceFn = void (*)(void* friends, std::uint64_t id);
using InviteUserToGameFn = bool (*)(void* friends, std::uint64_t id, const char* connect);
using RegisterCallbackFn = void (*)(void* callback, int id);
using SteamInitFn = bool (*)();
inline constexpr std::int32_t kFriendFlagImmediate = 0x04;
inline constexpr std::int32_t kFriendRelationshipFriend = 3;
// FriendGameInfo_t: CGameID m_gameID at +0 (app id in the low 24 bits); the
// caller's buffer is at least 32 bytes.
inline constexpr std::size_t kFriendGameInfoBuffer = 32;
inline constexpr char kRichPresenceKey[] = "mgscoop_join";

}  // namespace mgscoop::steam_net
