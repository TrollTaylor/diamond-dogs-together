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
//
// The file actions live in save_handoff::io (below), apart from the module
// state, so tests drive them on fake save trees (tests/save_handoff_io_tests).
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/save_handoff_core.hpp"
#include "mgscoop/save_handoff_wire.hpp"

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
// returned termination (0, true). A partner mission blocks PostExit; a title
// (4xxxx) or start-up mission (< 10000) is not one: the guest exits at the
// title, where the observer arms the title mission too.
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
// mid-transfer; frees the snapshot. Flags only here: the hook thread runs
// Core::OnShutdown and frees the memory on its next tick.
void Shutdown() noexcept;

// Tests only: forget the module state (not configured, no role, empty
// mailboxes) so one process can configure it again.
void ResetForTest() noexcept;

// ---- File actions (B7). Kernel32 only, wide paths, no lock of this module
// held; the module calls them on the hook thread. Every delete names a known
// file (a slot file, its WriteFileAtomic ".tmp", handoff.json, request.json)
// or an empty folder: nothing is ever removed recursively.
namespace io {

namespace wire = ::mgscoop::save_handoff_wire;
namespace core = ::mgscoop::save_handoff_core;

inline constexpr std::size_t kPathChars = 600;
struct Dirs {
    wchar_t save[kPathChars];     // <save_dir> (trailing separators trimmed)
    wchar_t handoff[kPathChars];  // <save_dir>-handoff (save_redirect format_handoff_dir)
    wchar_t rejoin[kPathChars];   // <game>\mgs-coop\link-rejoin.txt ("" without a game folder)
};
// UTF-8 save_dir (lab.ini) and the game folder (nullptr: no rejoin file).
bool MakeDirs(const char* save_dir, const wchar_t* directory, Dirs& out) noexcept;
// <save>\287700\local\<name> (slots 0..3) or <save>\remote\<name> (4, 5).
bool SlotFile(const Dirs& dirs, std::uint8_t slot, wchar_t* out, std::size_t capacity) noexcept;
// <handoff>\<XFER8>[.partial] and a slot file below it.
bool StageDir(const Dirs& dirs, std::uint32_t xfer, bool partial, wchar_t* out, std::size_t capacity) noexcept;
bool StageFile(const Dirs& dirs, std::uint32_t xfer, bool partial, std::uint8_t slot, wchar_t* out, std::size_t capacity) noexcept;
// <handoff>\<leaf> (provenance.json, request.json, applied.json).
bool HandoffFile(const Dirs& dirs, const wchar_t* leaf, wchar_t* out, std::size_t capacity) noexcept;

// Small text files: WriteText is lab::WriteFileAtomic (creating <handoff>
// first); ReadText refuses a file that does not fit `capacity - 1` (NUL added).
bool WriteText(const Dirs& dirs, const wchar_t* path, const char* text, std::size_t size) noexcept;
bool ReadText(const wchar_t* path, char* out, std::size_t capacity, std::size_t& size) noexcept;

// The on-disk set: presence, size and last-write time per slot, and whether
// another progress name (wire::IsProgressName outside the slot table) sits in
// 287700\local or remote. A missing folder lists as empty.
struct Stamp {
    bool present;
    std::uint64_t size, mtime;
};
struct Stamps {
    Stamp slot[wire::kSlots];
    bool unexpected;
};
bool StatSet(const Dirs& dirs, Stamps& out) noexcept;
// Per-slot hashes kept between hash jobs: a file is read again only when its
// size or last-write time changed.
struct HashCache {
    Stamp stamp[wire::kSlots];
    bool valid[wire::kSlots];
    std::uint8_t sha[wire::kSlots][wire::kShaSize];
};
// One hash pass, at most one file read per HashStep (local slots streamed
// with FILE_SHARE_READ|WRITE|DELETE, remote slots through
// save_redirect::read_remote_snapshot). A file over wire::kMaxFileBytes is not
// read and makes the total exceed wire::kMaxTotal (too_large); an empty or
// unreadable slot file gives ok=false; stable compares the stamps before and
// after the pass.
struct HashJob {
    bool active;
    std::uint8_t next;
    bool failed, oversize;
    Stamps before;
};
void HashBegin(HashJob& job) noexcept;
// True when the pass finished (out filled; the job is inactive again).
bool HashStep(const Dirs& dirs, HashJob& job, HashCache& cache, core::SetInfo& out) noexcept;
// A whole pass at once (tests).
bool HashSetNow(const Dirs& dirs, HashCache& cache, core::SetInfo& out) noexcept;

// Host snapshot: the present slots read into one VirtualAlloc block (at most
// wire::kMaxTotal), hashed (info), then verified by a second read.
struct SnapshotBuffer {
    std::uint8_t* base;
    std::size_t bytes;
    const std::uint8_t* data[wire::kSlots];
    core::SetInfo info;
};
bool SnapshotRead(const Dirs& dirs, SnapshotBuffer& out) noexcept;               // false: nothing kept
bool SnapshotVerify(const Dirs& dirs, const SnapshotBuffer& first) noexcept;     // the second read equals the first
void SnapshotFree(SnapshotBuffer& buffer) noexcept;

// Guest staging (P2-9): slot bytes into <XFER8>.partial (offset 0 truncates),
// handoff.json inside it, the commit (every manifest file re-read and
// checked, then MoveFileExW of the folder), and request.json last: refused
// unless the mode is scene or always and <XFER8> is committed with its
// handoff.json (a .partial is never ready).
bool StageWrite(const Dirs& dirs, std::uint32_t xfer, std::uint8_t slot, std::uint32_t offset, const std::uint8_t* data,
                std::size_t size) noexcept;
bool WriteStageManifest(const Dirs& dirs, std::uint32_t xfer, const char* text, std::size_t size) noexcept;
bool CommitStage(const Dirs& dirs, std::uint32_t xfer, const wire::FileEntry* files, unsigned n,
                 const std::uint8_t (&set)[wire::kShaSize]) noexcept;
bool WriteRequest(const Dirs& dirs, std::uint32_t xfer, wire::Mode mode, const char* text, std::size_t size) noexcept;
// request.json first (when it names this xfer or does not name one), then
// the known names of <XFER8>.partial and <XFER8> and their empty folders.
// True when no known file of the xfer is left (a foreign file keeps its folder).
bool DeleteStage(const Dirs& dirs, std::uint32_t xfer) noexcept;
// One staging folder (<handoff>\<XFER8>[.partial]): known names and empty
// folders only.
bool DeleteStageDir(const wchar_t* dir) noexcept;
// Configure: every <8 hex>.partial folder (DeleteStageDir) and a request.json
// whose pid (request_pid) is not own_pid or that does not parse. Returns the
// number of items removed.
unsigned CleanLeftovers(const Dirs& dirs, std::uint32_t own_pid,
                        bool (*request_pid)(const char* text, std::size_t size, std::uint32_t& pid) noexcept) noexcept;
// link-rejoin.txt (one MGS2 code + CRLF) when build() succeeds; the buffer is
// zeroed afterwards. False without a code or a game folder.
bool WriteRejoin(const Dirs& dirs, bool (*build)(char* out, std::size_t capacity) noexcept) noexcept;

// Exit request (P2-10): the largest visible unowned top-level window of
// `pid` gets WM_CLOSE. The enumerator and the poster are injectable.
struct Window {
    void* handle;
    std::uint32_t pid;
    bool visible, owned;
    std::int64_t area;
};
using EnumWindowsFn = unsigned (*)(Window* out, unsigned capacity) noexcept;
using PostCloseFn = bool (*)(void* handle) noexcept;
const Window* PickExitWindow(const Window* windows, unsigned count, std::uint32_t pid) noexcept;
// False: no window (how=no_window) or the post failed.
bool PostExit(EnumWindowsFn enumerate, PostCloseFn post, std::uint32_t pid) noexcept;
unsigned EnumTopLevelWindows(Window* out, unsigned capacity) noexcept;  // EnumWindows, this process's windows only
bool PostWindowClose(void* handle) noexcept;                            // PostMessageW(WM_CLOSE)

}  // namespace io

// P2-10 exit_slow: after a post_exit that found the window, a one-shot timer
// (timer queue, not the core or the link thread: the overlay's PartnerShutdown
// stops the core and a Steam link parks) logs "save_handoff=exit_slow" and
// posts G9 when the process is still alive core::kExitSlowMs later, unless the
// core left the exiting token first (a veto). The no_window path stays the core's.
// Tests only: the post_exit enumerator and poster and the exit_slow delay
// (nullptr / 0 restore the defaults; ResetForTest restores them too).
void SetExitForTest(io::EnumWindowsFn enumerate, io::PostCloseFn post, std::uint32_t slow_ms) noexcept;
// Tests only: executes one action on the hook-thread path (then drains the core).
void ExecuteForTest(const save_handoff_core::Action& action) noexcept;

}  // namespace mgscoop::save_handoff
