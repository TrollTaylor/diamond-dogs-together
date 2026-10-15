#pragma once
// Automatic save handoff glue (docs/save-handoff.md; join-fix phase 2 B7,
// mgs_coop_lab): one save_handoff_core::Core driven from the partner_net
// HandoffHooks on the link's receive thread, the file actions under
// <save_dir>-handoff (staging, provenance.json, request.json), the exit
// request (WM_CLOSE to this process's window) and the Lua SAVE receipt.
//
// Threads: HandleLua runs on the game's Lua runtime thread under the
// observer's receipt lock; it only parses, stores the report in a seqlock and
// replies. The hooks run on the receive thread with no partner_net lock held;
// nothing here holds a lock while calling partner_net, coop_hud, logging or
// touching a file. LuaCode/LockAllowed/GuestHandoffActive read published
// atomics only (any thread). Inert until Configure.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_net.hpp"

namespace mgscoop::save_handoff {

using LogFn = void (*)(const char* line) noexcept;

struct Config {
    std::uint8_t role;              // 1 host, 2 guest
    SaveHandoff mode;               // lab.ini save_handoff (absent = off)
    bool play;                      // mode=play
    bool net;                       // labc.net
    bool link_v2;                   // NetLink::v2
    bool saves_redirected;          // lab::SavesRedirected()
    bool world_on;                  // world >= 1 configured (world receipts flow)
    bool travel_lock;               // the effective lab.ini travel_lock (given ? value : play)
    bool steam_wait;                // guest: started waiting for an invite / F9 paste (link-rejoin.txt allowed)
    const char* save_dir;           // the redirected save_dir (no trailing separator needed)
    const wchar_t* directory;       // game folder (<dir>\mgs-coop\link-rejoin.txt)
    LogFn log;
};

// Install time (after world_link::Configure, before partner_net::Start).
// Always records the role and the travel lock (LockAllowed works with
// save_handoff=off). The transfer configures only when net, link v2, saves
// redirected, world_on, mode != off and the handoff dir fits; it logs
// "save_handoff=configured; role=%s; mode=%s; channel=4; dir=ok" or
// "save_handoff=disabled; reason=off|world_off|legacy_link|saves|path".
// Returns true when the transfer is configured (then the caller passes
// Hooks() to partner_net::SetHandoffHooks).
bool Configure(const Config& config) noexcept;
bool Configured() noexcept;
const partner_net::HandoffHooks* Hooks() noexcept;

// SAVE_V1 receipt (runtime thread, under the receipt lock): "return true"
// (accepted), "return false" (module off or rate-gated: more than 4 per
// second), nullptr (malformed: the literal stays).
const char* HandleLua(const char* source, std::size_t size) noexcept;
// Observer boundaries: the runtime mission entry (mission, false) and the
// returned termination (0, true). A partner mission blocks PostExit.
void NoteMission(std::uint32_t mission, bool terminating) noexcept;

// CFG reply r[12] (save_handoff_state::LuaCode; 0 when not configured).
std::uint8_t LuaCode(std::uint64_t now_ms) noexcept;
// CFG reply r[13]: 1 when the travel lock may apply (effective travel_lock,
// the F9 unlock not set, guest role), else 0.
std::uint8_t LockAllowed() noexcept;
// Guest role, mode scene or always, and a host STATE seen: world_link's NOTE
// code 1 posts no toast (the handoff toasts replace it).
bool GuestHandoffActive() noexcept;

// PartnerShutdown (before partner_net::Shutdown): ABORT shutdown when
// mid-transfer; frees the snapshot. Flags and the core only.
void Shutdown() noexcept;

}  // namespace mgscoop::save_handoff
