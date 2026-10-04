// Unit tests for the co-op lab identity and save proxies: the SteamFriends
// proxy in both roles (no loopback id ever reaches the real interface), the
// learned peer persona, the guest's fake GetSteamID, the armed-only auth ticket
// rewrite, the fail-closed GetUserDataFolder and the RemoteStorage proxy cache.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "mgscoop/save_redirect.hpp"
#include "mgscoop/steam_abi.hpp"
#include "mgscoop/steam_identity.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace mgscoop::lab;
namespace steam = mgscoop::steam;

namespace {
unsigned g_checks = 0;

// Bounded copy (avoids the CRT-deprecated strcpy/strncpy under /W4 /WX).
void CopyText(char* dst, std::size_t size, const char* src) {
    std::size_t i = 0;
    for (; src && src[i] && i + 1 < size; ++i) dst[i] = src[i];
    if (size) dst[i] = 0;
}
void Check(bool value, const char* what) {
    ++g_checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", g_checks, what);
        std::exit(1);
    }
}

constexpr std::uint64_t kHostId = 0x0110000100000001ull;
constexpr std::uint64_t kGuestFake = 0x01100001FFFFFFF0ull;  // 76561202255233008
constexpr std::uint64_t kStranger = 0x0110000100000777ull;

// ----- A fake interface whose every slot records that the real object was reached -----
struct FakeReal {
    const void** vtable;
    unsigned calls[80];
    std::uint64_t last_id;
};
FakeReal* g_real = nullptr;

std::uintptr_t MGSCOOP_STEAM_CALL generic_slot(void*) noexcept { return 0; }
const char* MGSCOOP_STEAM_CALL real_persona(void* self) noexcept {
    ++static_cast<FakeReal*>(self)->calls[steam::friends_slot::GetPersonaName];
    return "RealPersona";
}
const char* MGSCOOP_STEAM_CALL real_friend_persona(void* self, std::uint64_t id) noexcept {
    auto* r = static_cast<FakeReal*>(self);
    ++r->calls[steam::friends_slot::GetFriendPersonaName];
    r->last_id = id;
    return "RealFriend";
}
void MGSCOOP_STEAM_CALL real_overlay(void* self, const char*, std::uint64_t id) noexcept {
    auto* r = static_cast<FakeReal*>(self);
    ++r->calls[steam::friends_slot::ActivateGameOverlayToUser];
    r->last_id = id;
}
bool MGSCOOP_STEAM_CALL real_request_info(void* self, std::uint64_t id, bool) noexcept {
    auto* r = static_cast<FakeReal*>(self);
    ++r->calls[steam::friends_slot::RequestUserInformation];
    r->last_id = id;
    return true;
}
std::uint64_t* MGSCOOP_STEAM_CALL real_steamid(void* self, std::uint64_t* out) noexcept {
    ++static_cast<FakeReal*>(self)->calls[steam::user_slot::GetSteamID];
    if (out) *out = kHostId;
    return out;
}
bool MGSCOOP_STEAM_CALL real_user_folder(void* self, char* buf, int cub) noexcept {
    ++static_cast<FakeReal*>(self)->calls[steam::user_slot::GetUserDataFolder];
    if (buf && cub > 32) CopyText(buf, static_cast<std::size_t>(cub), "C:\\Steam\\userdata\\1\\287700\\local");
    return true;
}
std::uint32_t MGSCOOP_STEAM_CALL real_ticket(void* self, void* ticket, int cb_max, std::uint32_t* pcb) noexcept {
    ++static_cast<FakeReal*>(self)->calls[steam::user_slot::GetAuthSessionTicket];
    if (ticket && cb_max >= 32) {
        auto* bytes = static_cast<std::uint8_t*>(ticket);
        for (int i = 0; i < 32; ++i) bytes[i] = static_cast<std::uint8_t>(0xA0 + i);
        std::memcpy(bytes + 12, &kHostId, 8);
    }
    if (pcb) *pcb = 32;
    return 77;
}

void BuildVtable(const void** vt, unsigned count) {
    for (unsigned i = 0; i < count; ++i) vt[i] = reinterpret_cast<const void*>(&generic_slot);
}

template <typename Fn>
Fn slot(void* object, unsigned index) {
    return reinterpret_cast<Fn>((*static_cast<void* const* const*>(object))[index]);
}
using NameFn = const char*(MGSCOOP_STEAM_CALL*)(void*);
using FriendNameFn = const char*(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t);
using OverlayFn = void(MGSCOOP_STEAM_CALL*)(void*, const char*, std::uint64_t);
using InfoFn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t, bool);
using SteamIdFn = std::uint64_t*(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t*);
using FolderFn = bool(MGSCOOP_STEAM_CALL*)(void*, char*, int);
using TicketFn = std::uint32_t(MGSCOOP_STEAM_CALL*)(void*, void*, int, std::uint32_t*);

FakeReal MakeFriends(const void** vt) {
    BuildVtable(vt, steam::friends_slot::kCount);
    vt[steam::friends_slot::GetPersonaName] = reinterpret_cast<const void*>(&real_persona);
    vt[steam::friends_slot::GetFriendPersonaName] = reinterpret_cast<const void*>(&real_friend_persona);
    vt[steam::friends_slot::ActivateGameOverlayToUser] = reinterpret_cast<const void*>(&real_overlay);
    vt[steam::friends_slot::RequestUserInformation] = reinterpret_cast<const void*>(&real_request_info);
    FakeReal real{};
    real.vtable = vt;
    return real;
}
FakeReal MakeUser(const void** vt) {
    BuildVtable(vt, steam::user_slot::kCount);
    vt[steam::user_slot::GetSteamID] = reinterpret_cast<const void*>(&real_steamid);
    vt[steam::user_slot::GetUserDataFolder] = reinterpret_cast<const void*>(&real_user_folder);
    vt[steam::user_slot::GetAuthSessionTicket] = reinterpret_cast<const void*>(&real_ticket);
    FakeReal real{};
    real.vtable = vt;
    return real;
}

char g_log[1024]{};
void capture(const char* line) noexcept {
    const std::size_t have = std::strlen(g_log);
    CopyText(g_log + have, sizeof(g_log) - have, line);
}

void TestHostFriendsProxy() {
    identity::reset_for_test();
    identity::Settings s{};
    s.self_id = 0;  // host keeps its real identity
    s.peer_id = kGuestFake;
    s.loopback = true;
    Check(identity::needs_friends_proxy(s), "host loopback needs a friends proxy");
    Check(!identity::needs_user_proxy(s), "host without save_dir needs no user proxy");
    Check(identity::configure(s), "host settings accepted");
    static const void* vt[steam::friends_slot::kCount];
    static FakeReal real = MakeFriends(vt);
    void* proxy = identity::wrap_friends(&real);
    Check(proxy != &real && proxy != nullptr, "host gets a friends proxy");
    Check(identity::wrap_friends(&real) == proxy, "friends proxy cached per real pointer");

    // The guest's fake id is never passed to the real interface.
    const char* name = slot<FriendNameFn>(proxy, steam::friends_slot::GetFriendPersonaName)(proxy, kGuestFake);
    Check(std::strcmp(name, identity::kPeerFallbackName) == 0, "unknown peer persona -> fallback");
    Check(real.calls[steam::friends_slot::GetFriendPersonaName] == 0, "peer persona not forwarded");
    identity::note_peer(kGuestFake, "GuestPlayer");
    name = slot<FriendNameFn>(proxy, steam::friends_slot::GetFriendPersonaName)(proxy, kGuestFake);
    Check(std::strcmp(name, "GuestPlayer") == 0, "HELLO persona reported for the peer");
    Check(!slot<InfoFn>(proxy, steam::friends_slot::RequestUserInformation)(proxy, kGuestFake, false),
          "RequestUserInformation(peer) false");
    Check(real.calls[steam::friends_slot::RequestUserInformation] == 0, "peer info request not forwarded");
    slot<OverlayFn>(proxy, steam::friends_slot::ActivateGameOverlayToUser)(proxy, "steamid", kGuestFake);
    Check(real.calls[steam::friends_slot::ActivateGameOverlayToUser] == 0, "overlay to peer swallowed");

    // Everyone else still reaches real Steam.
    name = slot<FriendNameFn>(proxy, steam::friends_slot::GetFriendPersonaName)(proxy, kStranger);
    Check(std::strcmp(name, "RealFriend") == 0 && real.last_id == kStranger, "stranger persona forwarded");
    Check(slot<InfoFn>(proxy, steam::friends_slot::RequestUserInformation)(proxy, kStranger, true),
          "stranger info forwarded");
    slot<OverlayFn>(proxy, steam::friends_slot::ActivateGameOverlayToUser)(proxy, "steamid", kStranger);
    Check(real.calls[steam::friends_slot::ActivateGameOverlayToUser] == 1, "stranger overlay forwarded");
    // No self_name: the host's own persona is the real one.
    Check(std::strcmp(slot<NameFn>(proxy, steam::friends_slot::GetPersonaName)(proxy), "RealPersona") == 0,
          "host persona forwarded");
    // A persona change is published without touching the buffer last returned.
    const char* before = identity::peer_name();
    identity::note_peer(kGuestFake, "Renamed");
    Check(std::strcmp(before, "GuestPlayer") == 0 && std::strcmp(identity::peer_name(), "Renamed") == 0,
          "double-buffered persona update");
}

void TestLearnedPeer() {
    identity::reset_for_test();
    identity::Settings s{};
    s.loopback = true;  // peer_steamid=learn
    Check(identity::configure(s), "learn settings accepted");
    Check(!identity::is_loopback_id(kGuestFake), "nothing learned yet");
    identity::note_peer(kGuestFake, "");
    Check(identity::is_loopback_id(kGuestFake), "learned peer is a loopback id");
    Check(std::strcmp(identity::peer_name(), identity::kPeerFallbackName) == 0, "no name yet -> fallback");
    identity::note_peer(123, "bogus");
    Check(!identity::is_loopback_id(123), "non-individual ids ignored");
}

void TestGuestIdentity() {
    identity::reset_for_test();
    identity::Settings bad{};
    bad.self_id = kGuestFake;
    bad.loopback = false;
    Check(!identity::configure(bad), "fake id without loopback refused");
    bad.loopback = true;
    bad.self_id = 42;
    Check(!identity::configure(bad), "non-individual fake id refused");

    identity::reset_for_test();
    g_log[0] = 0;
    identity::Settings s{};
    s.self_id = kGuestFake;
    s.peer_id = kHostId;
    s.loopback = true;
    CopyText(s.self_name, sizeof(s.self_name), "LabGuest");
    s.log = &capture;
    Check(identity::configure(s), "guest settings accepted");
    static const void* uvt[steam::user_slot::kCount];
    static FakeReal user = MakeUser(uvt);
    void* proxy = identity::wrap_user(&user);
    Check(proxy != &user, "guest gets a user proxy");
    std::uint64_t id = 0;
    Check(slot<SteamIdFn>(proxy, steam::user_slot::GetSteamID)(proxy, &id) == &id && id == kGuestFake,
          "GetSteamID returns the fake id through the hidden return pointer");
    Check(user.calls[steam::user_slot::GetSteamID] == 0, "real GetSteamID not consulted");
    char folder[64]{};
    Check(slot<FolderFn>(proxy, steam::user_slot::GetUserDataFolder)(proxy, folder, sizeof(folder)) &&
              user.calls[steam::user_slot::GetUserDataFolder] == 1,
          "no save_dir: GetUserDataFolder forwarded");

    // Ticket: untouched until armed, then bytes 12..19 carry the fake id.
    std::uint8_t ticket[64]{};
    std::uint32_t size = 0;
    Check(slot<TicketFn>(proxy, steam::user_slot::GetAuthSessionTicket)(proxy, ticket, sizeof(ticket), &size) == 77,
          "ticket handle passed through");
    std::uint64_t embedded = 0;
    std::memcpy(&embedded, ticket + 12, 8);
    Check(embedded == kHostId && ticket[0] == 0xA0 && ticket[20] == 0xA0 + 20, "unarmed: ticket untouched");
    Check(std::strstr(g_log, "identity=ticket_unrewritten; reason=master_redirect_inactive") != nullptr,
          "unarmed rewrite logged");
    identity::arm_ticket_rewrite(true);
    Check(identity::ticket_rewrite_armed(), "armed");
    std::memset(ticket, 0, sizeof(ticket));
    slot<TicketFn>(proxy, steam::user_slot::GetAuthSessionTicket)(proxy, ticket, sizeof(ticket), &size);
    std::memcpy(&embedded, ticket + 12, 8);
    Check(embedded == kGuestFake && ticket[11] == 0xA0 + 11 && ticket[20] == 0xA0 + 20,
          "armed: only bytes 12..19 rewritten");

    // Friends: the guest's own fake id reports its persona; the host id is the peer.
    static const void* fvt[steam::friends_slot::kCount];
    static FakeReal friends = MakeFriends(fvt);
    void* fproxy = identity::wrap_friends(&friends);
    Check(std::strcmp(slot<NameFn>(fproxy, steam::friends_slot::GetPersonaName)(fproxy), "LabGuest") == 0,
          "guest persona override");
    Check(std::strcmp(slot<FriendNameFn>(fproxy, steam::friends_slot::GetFriendPersonaName)(fproxy, kGuestFake),
                      "LabGuest") == 0,
          "persona for the fake self id");
    Check(!slot<InfoFn>(fproxy, steam::friends_slot::RequestUserInformation)(fproxy, kGuestFake, false) &&
              friends.calls[steam::friends_slot::RequestUserInformation] == 0,
          "fake self id never sent to real Steam");
    Check(friends.calls[steam::friends_slot::GetFriendPersonaName] == 0, "no friend persona forwarded");
}

void TestUserDataFolderFailsClosed() {
    identity::reset_for_test();
    g_log[0] = 0;
    identity::Settings s{};
    s.log = &capture;
    // 250 characters: '<dir>\287700\local' cannot fit the game's 0x105-byte buffer.
    std::memset(s.save_dir, 'a', 250);
    s.save_dir[0] = 'D';
    s.save_dir[1] = ':';
    s.save_dir[2] = '\\';
    Check(!save::save_dir_fits(s.save_dir), "250-character save_dir rejected by the early check");
    Check(identity::configure(s), "settings accepted by identity");
    static const void* uvt[steam::user_slot::kCount];
    static FakeReal user = MakeUser(uvt);
    void* proxy = identity::wrap_user(&user);
    char folder[0x105];
    std::memset(folder, 'x', sizeof(folder));
    Check(!slot<FolderFn>(proxy, steam::user_slot::GetUserDataFolder)(proxy, folder, 0x105),
          "redirect that does not fit fails");
    Check(folder[0] == 0, "buffer emptied");
    Check(user.calls[steam::user_slot::GetUserDataFolder] == 0, "real userdata folder never handed out");
    Check(std::strstr(g_log, "saves=refused; reason=user_data_folder_does_not_fit") != nullptr, "refusal logged");
    // A fitting redirect is formatted.
    identity::reset_for_test();
    identity::Settings ok{};
    CopyText(ok.save_dir, sizeof(ok.save_dir), "D:\\lab\\saves");
    Check(save::save_dir_fits(ok.save_dir), "short save_dir fits");
    Check(identity::configure(ok), "save-only settings");
    static const void* uvt2[steam::user_slot::kCount];
    static FakeReal user2 = MakeUser(uvt2);
    void* proxy2 = identity::wrap_user(&user2);
    Check(slot<FolderFn>(proxy2, steam::user_slot::GetUserDataFolder)(proxy2, folder, 0x105) &&
              std::strcmp(folder, "D:\\lab\\saves\\287700\\local") == 0,
          "host save-only proxy redirects the folder");
    std::uint64_t id = 0;
    slot<SteamIdFn>(proxy2, steam::user_slot::GetSteamID)(proxy2, &id);
    Check(id == kHostId && user2.calls[steam::user_slot::GetSteamID] == 1, "save-only proxy keeps the real id");
}

void TestRemoteStorageCache() {
    save::reset_for_test();
    char temp[MAX_PATH]{};
    Check(GetTempPathA(MAX_PATH, temp) > 0, "temp path");
    char dir[MAX_PATH]{};
    std::snprintf(dir, sizeof(dir), "%smgscoop-save-test-%lu", temp, GetCurrentProcessId());
    save::configure(dir);
    Check(save::configured(), "save redirect configured");
    static const void* vt[steam::storage_slot::kCount];
    BuildVtable(vt, steam::storage_slot::kCount);
    static FakeReal storage{};
    storage.vtable = vt;
    void* first = save::wrap_remote_storage(&storage);
    void* second = save::wrap_remote_storage(&storage);
    Check(first != &storage && first == second, "one RemoteStorage proxy per real pointer");
    // Clean up the directories configure created.
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\287700\\local", dir);
    RemoveDirectoryA(path);
    std::snprintf(path, sizeof(path), "%s\\287700", dir);
    RemoveDirectoryA(path);
    std::snprintf(path, sizeof(path), "%s\\remote", dir);
    RemoveDirectoryA(path);
    RemoveDirectoryA(dir);
    save::reset_for_test();
}
}  // namespace

int main() {
    TestHostFriendsProxy();
    TestLearnedPeer();
    TestGuestIdentity();
    TestUserDataFolderFailsClosed();
    TestRemoteStorageCache();
    std::printf("steam_identity_tests: %u checks passed\n", g_checks);
    return 0;
}
