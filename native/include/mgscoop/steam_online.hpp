#pragma once
// Steam online owner context (docs/steam-online.md sections 5, 10, 14).
//
// The loader's only callers of Steam's flat User/Friends/Client exports run
// here, in the post-RunCallbacks hook (steam_observer observed_callbacks):
// OwnerPass takes a try-lock (one of the game's callback threads at a time)
// and spends at most 4 Steam calls per pass on:
//   - the one-time acquisition and its gates (api, not_initialized,
//     client_module, client_iface, offline, self_mismatch, peer_is_self,
//     iface_missing, iface_module, iface_rtti, iface_shape; fault),
//   - the peer's persona name, the friend list (only while the F9 Invite page
//     is open, at most every 5 s), InviteUserToGame, the rich-presence beacon
//     (key mgscoop_join only; ClearRichPresence is never called),
//   - starting the lab self-test thread (steam_selftest=1|2).
// The interfaces are cached forever (every GetISteamGenericInterface call
// allocates an adapter). Messages, Sockets and Utils are called only from the
// transport and self-test threads. Every failure latches
// "steam=disabled; reason=<r>" and the transport reports
// "link=disabled; reason=steam_<r>".
#include <cstddef>
#include <cstdint>
#include "mgscoop/steam_transport.hpp"

namespace mgscoop::steam_online {

using LogFn = void (*)(const char* line) noexcept;

struct Config {
    bool play_steam = false;         // mode=play, net=1, net_transport=steam (and the saves gate passed)
    bool host = false;
    std::uint64_t expected_self = 0;  // lab.ini net_steam_self (gate 5)
    std::uint64_t host_id = 0;        // guest: net_peer_steam (0 = waiting)
    std::uint32_t selftest = 0;       // lab steam_selftest (0 off, 1 pair, 2 + network loopback)
    bool fake_identity = false;       // lab self_steamid set: the self-test refuses
    LogFn log = nullptr;              // script-observer.log
};

#ifdef _WIN32
// steam_observer install (outside DllMain): the steam_api64 module it hashed
// (full SHA-256 at the game folder) and whether RunCallbacks is wrapped.
void NoteApiModule(void* steam_api, bool sha_verified) noexcept;  // HMODULE
void NoteOwnerHook(bool installed) noexcept;
// Script observer install, once. Without play_steam or a self-test it stays inert.
void Configure(const Config& config) noexcept;
// observed_callbacks, after the real RunCallbacks (game callback threads).
void OwnerPass() noexcept;
// lab::SteamResolveFn for the transport: 1 ready, -1 disabled (reason), 0 pending.
int Resolve(void* ctx, lab::SteamNetApi& out, const char*& reason) noexcept;
// Window-close shutdown flag (flag only): the next owner pass clears the
// guest beacon once, then the owner context makes no further Steam call.
void Shutdown() noexcept;
// The steam_status reply / state.json data.
bool StatusLine(char* out, std::size_t capacity) noexcept;
#endif

}  // namespace mgscoop::steam_online
