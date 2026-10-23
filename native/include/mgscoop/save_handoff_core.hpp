#pragma once
// Automatic save handoff core (docs/save-handoff.md; join-fix phase 2 P2-7..
// P2-10, track A2): the host/guest state machine, the decision helpers, the
// provenance table and the JSON documents, deterministic with a caller clock.
// Pure (mgs_coop_core): no Windows headers, no allocation, no I/O. The glue
// (save_handoff.cpp, track B7) feeds the inputs on the link's receive thread
// and executes the Actions it pops, in order, reporting the results back.
//
// Calling convention: one thread; after EVERY input call the caller drains the
// ring (PopAction until false), executes each action in order and reports the
// result of an action that has a report (see ActionKind) before popping the
// next one. A report may queue more actions; the same drain loop runs them.
#include <cstddef>
#include <cstdint>
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/save_handoff_state.hpp"
#include "mgscoop/save_handoff_wire.hpp"

namespace mgscoop::save_handoff_core {

namespace wire = ::mgscoop::save_handoff_wire;

// ---- Timers and bounds (A2).
inline constexpr std::uint64_t kStateMinGapMs = 500;      // STATE on change, at least this far apart
inline constexpr std::uint64_t kStateHeartbeatMs = 5000;  // and this heartbeat (plus at every new session)
inline constexpr std::uint64_t kOfferIdleMs = 30000;      // host: an OFFER without WANT/DECLINE expires
inline constexpr std::uint64_t kChunkStallMs = 30000;     // guest: no CHUNK for this long -> ABORT stall;
                                                          // host: no CHUNK queued for this long while sending -> failed stall
inline constexpr std::uint64_t kResultWaitMs = 60000;     // host: END sent, no RESULT -> failed timeout
inline constexpr std::uint64_t kSnapshotKeepMs = 300000;  // host: the in-memory snapshot is released after this
inline constexpr std::uint64_t kSnapshotGapMs = 500;      // the two snapshot / Continue-bind reads are this far apart
inline constexpr std::uint64_t kCountdownMs = 10000;      // guest: title countdown before PostExit
inline constexpr std::uint64_t kTitleLostMs = 2000;       // guest: title lost this long during the countdown -> ABORT not_title
inline constexpr std::uint64_t kExitSlowMs = 20000;       // guest: still alive this long after PostExit -> exit_slow (+ G9):
                                                          // the core for no_window, the glue's timer after a WM_CLOSE
inline constexpr std::uint64_t kExitFreshMs = 1500;       // Lua report freshness for PostExit
// guest: after RESULT exiting, PostExit waits until channel 4 is empty (every
// message acknowledged) or this long (online 2026-10-22 P4: a Steam link parks
// at the WM_CLOSE, and the unsent RESULT never told the host about the restart)
inline constexpr std::uint64_t kExitFlushMs = 1000;
inline constexpr std::uint64_t kPhaseFreshMs = 3000;      // Lua report freshness for phase/story (older: unknown)
// In-game settle bind window (30010/30020/30050). 30 s until 0.13.5; 10 s
// since online 2026-10-22 P3 (a guest waited at its title for the host's 30 s
// in Mother Base). The window was never the guard against a set saved for the
// next mission: any mission load reports loading at once (world_handoff
// X.leaving), which ends the window before the vanilla load, and the set must
// stay unchanged with saving=0 and fresh reports for the whole window.
inline constexpr std::uint64_t kSettleBindMs = 10000;
inline constexpr std::uint64_t kBadMsgLogMs = 10000;      // "save_handoff=bad_msg" at most this often
inline constexpr std::uint64_t kRehashMs = 2000;          // re-hash the on-disk set this often (the glue skips unchanged files)
inline constexpr std::uint64_t kHashTimeoutMs = 10000;    // a hash_set / snapshot without its report may be asked again
inline constexpr std::uint64_t kRetryGapMs = 2000;        // host: after a failed transfer, before the next OFFER
inline constexpr std::uint64_t kContinueBindMs = 60000;   // a Continue bind gives up when no two equal reads came within this
inline constexpr unsigned kMaxOffersPerSet = 3;           // per set per link session
inline constexpr unsigned kChunksPerTick = 4;             // new CHUNKs per Tick ...
inline constexpr unsigned kMaxQueued = 16;                // ... while ReliableQueued(4) is below this
inline constexpr std::size_t kActionCapacity = 64;        // ring size (an overflow drops and counts)
inline constexpr std::size_t kLogChars = 192;             // log text without "; t_ms=N"
inline constexpr std::size_t kUtcCapacity = 32;           // "yyyy-MM-ddTHH:mm:ssZ" (seconds, UTC) + NUL
inline constexpr std::size_t kBuildChars = 32;            // link remote_build copy (handoff.json host_build)

// ---- Scenes: world_follow's SUPPORTED map (lua/coop/world_follow.lua).
// Loc: 30010/40010 -> 10, 30020/40020 -> 20, 30050/40050 -> 50, else 0.
std::uint8_t Loc(std::uint32_t mission) noexcept;
inline bool Supported(std::uint32_t mission) noexcept { return Loc(mission) != 0; }
// The in-game settle bind missions: 30010, 30020, 30050.
bool SettleMission(std::uint32_t mission) noexcept;

// ---- The Lua SAVE receipt (P2-2; world_receipt ParseSave). op 1 REPORT.
struct Report {
    std::uint32_t mission;  // vars.missionCode 0..99999
    bool title;             // gvars.ini_isTitleMode and a 4xxxx mission
    wire::Story story;      // str_storySequence class
    bool saving;            // TppScriptVars.IsSavingOrLoading()
    bool ingame;
    std::uint64_t at_ms;    // when the receipt arrived (caller clock; 0 = the OnLua now_ms). Feeding the same
                            // report again with the same at_ms is harmless.
};
// op 3 DECLINE reasons (Lua a=1, F9 Keep my own save = 2).
enum class DeclineSource : std::uint8_t { second_continue = 1, f9 = 2 };

// ---- P2-7 decisions (pure).
struct HostFacts {
    wire::Mode mode;              // off -> Elig::off
    bool report_fresh;            // the Lua report is <= kPhaseFreshMs old (stale counts as story_unknown)
    wire::Story story;
    bool saving;
    bool set_known;               // the current set is hashed (false -> continue_unknown)
    bool unexpected;              // another progress name in the save folders
    std::uint64_t total;          // bytes of the present slots
    std::uint32_t bound_mission;  // Provenance mission of the current set (0 unbound)
};
// eligible only when all hold; otherwise the first failing reason in the
// order off, story_unknown, story_opening, unexpected_files, too_large,
// saving, continue_unknown, continue_unsupported.
wire::Elig HostEligible(const HostFacts& facts) noexcept;

enum class NeedWhy : std::uint8_t { off, same, same_scene, story, unsupported, location, always };
const char* NeedWhyName(NeedWhy why) noexcept;  // off, same, same_scene, story, unsupported, location, always
struct SideFacts {
    bool set_known;
    std::uint8_t set[wire::kShaSize];
    wire::Story story;
    std::uint32_t mission;        // own: the title (ACC) mission; host: its STATE mission
    std::uint32_t bound_mission;  // provenance of that side's set (0 unknown)
};
// 0 when policy is off (why off) or the sets are equal (why same). Otherwise
// always gives 1 (why always); scene and stage give 1 when own.story != open
// (story), own.bound_mission is nonzero and not Supported (unsupported), or
// Loc(own.mission) != Loc(host.bound_mission, or host.mission when unbound)
// (location); else 0 (same_scene).
std::uint8_t GuestNeed(wire::Mode policy, const SideFacts& own, const SideFacts& host, NeedWhy& why) noexcept;

// ---- P2-10 exit gate (pure).
struct ExitFacts {
    wire::Mode mode;               // scene or always only
    bool staged;                   // the verified stage is committed
    bool request_written;
    bool result_sent;              // RESULT staged was queued
    bool report_fresh;             // the Lua report is <= kExitFreshMs old
    bool title, saving, ingame;    // from that report: title=1, saving=0, ingame=0 required
    bool partner_mission;          // save_handoff::NoteMission: a partner mission is armed
    bool terminating;
    std::uint64_t title_held_ms;   // countdown elapsed with the title held throughout (>= kCountdownMs)
    bool declined;                 // Lua op 3 or the F9 Keep my own save request
};
bool ExitGate(const ExitFacts& facts) noexcept;

// ---- P2-8 provenance (both roles): which mission an on-disk set continues into.
enum class Source : std::uint8_t { none, continue_, settle };
const char* SourceName(Source source) noexcept;  // "none", "continue", "settle"
inline constexpr unsigned kProvenanceEntries = 8;
struct ProvenanceEntry {
    std::uint8_t set[wire::kShaSize];
    std::uint32_t mission;
    Source source;
    char utc[kUtcCapacity];
};
// <save_dir>-handoff\provenance.json:
// {"format":"mgs-coop-save-provenance","v":1,"entries":[{"set":"<64 HEX>","mission":30010,"source":"continue|settle","utc":"...Z"}]}
inline constexpr std::size_t kProvenanceJsonMax = 1536;
class Provenance {
public:
    // set -> mission as the most recently used entry (an entry for the same
    // set is replaced; when full the least recently used one is dropped).
    void Bind(const std::uint8_t (&set)[wire::kShaSize], std::uint32_t mission, Source source, const char* utc) noexcept;
    // The entry for set, nullptr when unbound.
    const ProvenanceEntry* Find(const std::uint8_t (&set)[wire::kShaSize]) const noexcept;
    unsigned Count() const noexcept;
    // 0 = most recently used; index < Count().
    const ProvenanceEntry& At(unsigned index) const noexcept;
    void Clear() noexcept;
    // Most recent first; 0 (out[0] = 0) when it does not fit.
    std::size_t Format(char* out, std::size_t capacity) const noexcept;
    // Replaces the table. False (table cleared) for malformed text, an unknown
    // format or v, a bad hex set, mission 0 or > 99999, or more than 8 entries.
    bool Parse(const char* text, std::size_t size) noexcept;

private:
    ProvenanceEntry entries_[kProvenanceEntries]{};
    unsigned count_ = 0;
};

// ---- P2-9 staging documents (bounded, ASCII; parse rejects an unknown format or v).
// handoff.json inside <xfer8hex>.partial:
// {"format":"mgs-coop-save-handoff","v":1,"xfer":"0A1B2C3D","created_utc":"...Z","host_build":"<remote_build>",
//  "host_mission":30010,"set_sha256":"<64 HEX>","files":[{"entry":"287700/local/TPP_GAME_DATA0","bytes":199916,"sha256":"<64 HEX>"}]}
struct Manifest {
    std::uint32_t xfer;
    char created_utc[kUtcCapacity];
    char host_build[kBuildChars];
    std::uint32_t host_mission;
    std::uint8_t set[wire::kShaSize];
    std::uint8_t n;
    wire::FileEntry files[wire::kMaxFiles];
};
inline constexpr std::size_t kManifestJsonMax = 2048;
std::size_t FormatManifest(const Manifest& manifest, char* out, std::size_t capacity) noexcept;
bool ParseManifest(const char* text, std::size_t size, Manifest& out) noexcept;

// request.json (written last, scene/always only):
// {"format":"mgs-coop-save-handoff-request","v":1,"pid":N,"process_start_utc":"...Z","xfer":"0A1B2C3D",
//  "set_sha256":"<64 HEX>","mode":"scene|always","rejoin":0|1,"created_utc":"...Z"}
struct Request {
    std::uint32_t pid;
    char process_start_utc[kUtcCapacity];
    std::uint32_t xfer;
    std::uint8_t set[wire::kShaSize];
    wire::Mode mode;      // scene | always
    std::uint8_t rejoin;  // 0|1
    char created_utc[kUtcCapacity];
};
inline constexpr std::size_t kRequestJsonMax = 512;
std::size_t FormatRequest(const Request& request, char* out, std::size_t capacity) noexcept;
bool ParseRequest(const char* text, std::size_t size, Request& out) noexcept;

// applied.json (written by the tooling after it applied a stage):
// {"format":"mgs-coop-save-handoff-applied","v":1,"set_sha256":"..","applied_utc":"..","aside":"<leaf>"}
struct Applied {
    std::uint8_t set[wire::kShaSize];
    char applied_utc[kUtcCapacity];
    char aside[96];
};
inline constexpr std::size_t kAppliedJsonMax = 512;
std::size_t FormatApplied(const Applied& applied, char* out, std::size_t capacity) noexcept;
bool ParseApplied(const char* text, std::size_t size, Applied& out) noexcept;

// ---- Glue reports.
// The on-disk set as the glue hashed it (HashSet; one file per tick, only
// while the Lua report is fresh with saving=0).
struct SetInfo {
    bool ok;                   // read without error, slot 0 or 1 present (the guest: or slot 4, an old
                               // Steam save with remote files only; online 2026-10-22 P1)
    bool stable;               // sizes and mtimes equal before and after the read
    bool unexpected;           // another IsProgressName file sits in 287700\local or remote
    std::uint64_t total;       // sum of the present sizes (may exceed kMaxTotal)
    std::uint8_t n;
    wire::FileEntry files[wire::kMaxFiles];  // ascending slots
    std::uint8_t set[wire::kShaSize];        // SetSha256(files, n)
};
// Host snapshot: the set read into memory twice kSnapshotGapMs apart (the two
// reads equal, else ok=false). data[slot] stays valid until release_snapshot.
struct Snapshot {
    SetInfo info;
    const std::uint8_t* data[wire::kSlots];
};

// ---- Actions.
enum class ActionKind : std::uint8_t {
    none,
    send,              // partner_net::SendReliable(kChannel, data, size, latest ? latest : append); report OnSendResult
                       // (latest only for a STATE that follows a STATE: latest replaces ANY unsent message)
    hash_set,          // hash the on-disk set; report OnSetHashed (later ticks). Idempotent: a repeat while one is
                       // in progress (asked again after kHashTimeoutMs) may be merged into it
    snapshot,          // host: read the current set into memory; report OnSnapshotRead (later ticks)
    release_snapshot,  // host: free the snapshot memory (no report)
    stage_write,       // guest: data[0..size) at `offset` of `slot` in <xfer>.partial; report OnStageDone
    write_manifest,    // guest: handoff.json = FormatManifest(manifest()) inside <xfer>.partial; report OnStageDone
    commit_stage,      // guest: MoveFileExW <xfer>.partial -> <xfer>; report OnStageDone
    write_request,     // guest: request.json = FormatRequest(request()) (last); report OnStageDone
    write_rejoin,      // guest: link-rejoin.txt when partner_net::BuildGuestJoinCode succeeds (no report)
    write_provenance,  // provenance.json = provenance().Format (WriteFileAtomic; no report)
    delete_stage,      // guest: request.json first, then the known names and empty folders of <xfer>.partial and
                       // <xfer> (whichever exists); report OnStageDone
    post_exit,         // guest: EnumWindows + PostMessageW(WM_CLOSE); report OnExitPosted (queued once RESULT
                       // exiting left channel 4, at most kExitFlushMs after it)
    toast,             // coop_hud::Post(EventKind::save_handoff, code, "<a> <b>") (no report)
    log,               // log text + "; t_ms=N" (no report)
    publish,           // save_handoff_state::Publish(view) + partner_net::PublishHandoffState (no report)
};
const char* ActionName(ActionKind kind) noexcept;

struct Action {
    ActionKind kind;
    std::uint32_t xfer;               // stage_write .. delete_stage
    wire::Kind message;               // send: the message kind in data
    bool latest;                      // send: ReliableMode::latest (STATE) instead of append
    std::uint8_t slot;                // stage_write; send of a CHUNK
    std::uint32_t offset;             // stage_write; send of a CHUNK
    std::uint16_t size;               // send / stage_write: bytes used in data
    std::uint8_t rejoin;              // write_request
    std::uint8_t code;                // toast: save_handoff_state::Toast
    std::uint32_t a, b;               // toast
    save_handoff_state::View view;    // publish
    char text[kLogChars];             // log: "save_handoff=..." (no t_ms)
    std::uint8_t data[wire::kMaxMessage];  // send: the encoded message; stage_write: the payload
};

struct Stats {
    std::uint32_t bad_msg;            // dropped peer messages (any Decode != ok, or out of state)
    wire::Decode last_bad;
    std::uint32_t offers, chunks_sent, chunks_received;
    std::uint32_t dropped_actions;    // ring overflow (a bug)
    std::uint32_t stale;              // messages of an older transfer (expected after an abort; not logged)
};

struct Config {
    wire::Role role = wire::Role::none;
    wire::Mode mode = wire::Mode::off;  // the lab.ini policy (stage decides like scene)
    bool rejoin = false;                // guest: a steam-wait guest (request.json rejoin, write_rejoin before exit)
    std::uint32_t pid = 0;              // request.json pid
    char process_start_utc[kUtcCapacity]{};
    // Wall clock for the documents ("yyyy-MM-ddTHH:mm:ssZ"); nullptr gives
    // "1970-01-01T00:00:00Z" (tests).
    void (*utc_now)(char (&out)[kUtcCapacity]) noexcept = nullptr;
    // Nonzero transfer ids (BCryptGenRandom in the glue); nullptr gives a
    // deterministic sequence (tests).
    std::uint32_t (*random_u32)() noexcept = nullptr;
};

// About 90 KB (the Action ring): keep it in static storage, not on a stack.
class Core {
public:
    Core() noexcept = default;
    explicit Core(const Config& config) noexcept { Reset(config); }
    // Forgets everything (the provenance table too) and starts with `config`.
    void Reset(const Config& config) noexcept;

    // ---- Inputs (one thread; drain the actions after each call).
    void OnLua(const Report& report, std::uint64_t now_ms) noexcept;
    // Lua op 2 CONTINUE: a Continue load into `target` (binds the set, P2-8 a).
    // A guest that is already exiting (the WM_CLOSE did not close the game)
    // loads its own save: the stage is dropped (ABORT not_title, request.json first).
    void OnContinue(std::uint32_t target, std::uint64_t now_ms) noexcept;
    // Lua op 3 DECLINE or the F9 Keep my own save request: in every guest phase
    // up to exiting the stage is dropped (ABORT user, request.json first).
    void OnDecline(DeclineSource source, std::uint64_t now_ms) noexcept;
    // One channel-4 message from the peer (any Decode failure is counted).
    void OnPeer(const std::uint8_t* bytes, std::size_t size, std::uint64_t now_ms) noexcept;
    // A new link session (no resume: the partial transfer is dropped, a
    // verified stage is kept); STATE goes out again.
    void OnSession(std::uint32_t session, std::uint64_t now_ms) noexcept;
    void OnPeerClose(std::uint64_t now_ms) noexcept;
    // The peer build gate (RemoteBuildAtLeast(kHandoffMin*)); false sends nothing at all.
    void OnBuildOk(bool ok, const char* remote_build) noexcept;
    void OnSetHashed(const SetInfo& info, std::uint64_t now_ms) noexcept;
    // snapshot is nullptr when !ok.
    void OnSnapshotRead(bool ok, const Snapshot* snapshot, std::uint64_t now_ms) noexcept;
    // The result of the last popped stage_write / write_manifest / commit_stage /
    // write_request / delete_stage.
    void OnStageDone(bool ok, std::uint64_t now_ms) noexcept;
    // The result of a popped send (queued or replaced = true; full,
    // not_connected, ... = false). After the first refused CHUNK of a drain the
    // glue reports the remaining CHUNK sends of that drain refused without
    // sending them; Core rewinds its cursor to the first refused one.
    void OnSendResult(const Action& sent, bool queued, std::uint64_t now_ms) noexcept;
    void OnExitPosted(bool window_found, std::uint64_t now_ms) noexcept;
    // save_handoff::NoteMission: a partner mission is armed (nonzero) / ending.
    void OnPartnerMission(std::uint32_t mission, bool terminating, std::uint64_t now_ms) noexcept;
    // Guest Configure found applied.json from the tooling (toast G10, token applied).
    void OnAppliedSeen(const Applied& applied, std::uint64_t now_ms) noexcept;
    // provenance.json at Configure; false (queues write_provenance) when malformed.
    bool LoadProvenance(const char* text, std::size_t size) noexcept;
    // The game is closing (save_handoff::Shutdown).
    void OnShutdown(std::uint64_t now_ms) noexcept;
    // Timers, pacing (`queued` = partner_net::ReliableQueued(kChannel)) and STATE.
    void Tick(std::uint64_t now_ms, unsigned queued) noexcept;

    // ---- Outputs.
    bool PopAction(Action& out) noexcept;

    // ---- Observers.
    const save_handoff_state::View& view() const noexcept { return view_; }
    const Provenance& provenance() const noexcept { return provenance_; }
    const Manifest& manifest() const noexcept { return manifest_; }  // the current stage
    const Request& request() const noexcept { return request_; }
    Stats stats() const noexcept { return stats_; }

private:
    // Track A owns the private layout; it is not part of the contract.
    enum class HostPhase : std::uint8_t { idle, snapshot, offered, sending, end_sent, done };
    enum class GuestPhase : std::uint8_t { idle, receiving, finishing, staged, countdown, exiting };
    struct Machine {
        // Link.
        bool linked = false;
        std::uint32_t session = 0;
        bool build_known = false, build_ok = false;
        char remote_build[kBuildChars] = {};
        bool shutdown = false;
        // The Lua report and the observer boundaries.
        bool have_report = false;
        Report report{};
        bool partner_mission = false, terminating = false;
        // The own on-disk set.
        SetInfo set{};
        bool set_known = false, set_seen = false;
        std::uint64_t set_at = 0;
        bool hash_pending = false;
        std::uint64_t hash_at = 0;
        // P2-8 (a) Continue bind: two equal reads at least kSnapshotGapMs apart.
        bool cb_active = false, cb_first = false;
        std::uint32_t cb_target = 0;
        std::uint64_t cb_at = 0, cb_first_at = 0;
        std::uint8_t cb_set[wire::kShaSize] = {};
        // P2-8 (b) settle bind.
        bool st_active = false, st_have = false, st_bound = false;
        std::uint32_t st_mission = 0;
        std::uint64_t st_set_at = 0;
        std::uint8_t st_set[wire::kShaSize] = {};
        // The peer's STATE (this link session).
        bool peer_seen = false;
        wire::State peer{};
        bool peer_logged = false;
        wire::State peer_log{};
        // Our STATE.
        bool state_force = true, state_sent = false;
        // ReliableMode::latest replaces the newest unsent message of ANY kind
        // (link_protocol ReliableSender::Queue), so a STATE uses it only when
        // the previous send of this core was a STATE that was not refused.
        bool last_send_state = false;
        std::uint64_t state_at = 0;
        std::uint8_t state_last[wire::kStateSize] = {};
        std::uint64_t bad_log_at = 0;
        bool bad_logged = false;
        // The transfer (both roles).
        std::uint32_t xfer = 0, xfer_seq = 0;
        wire::Offer offer{};
        std::uint8_t offers_set[wire::kShaSize] = {};
        unsigned offers = 0;              // per set per link session (host: sent, guest: received)
        bool sticky = false;              // the token stays until the next transfer starts
        std::uint8_t toasted = 0xFF;      // the refusal / wait reason already toasted this session (kSettleToast:
                                          // continue_unknown while the host settles, G11 / H8)
        // Host.
        HostPhase host = HostPhase::idle;
        wire::Elig elig = wire::Elig::unknown;
        bool snap_held = false;
        Snapshot snap{};
        std::uint64_t snap_at = 0;
        std::uint8_t snap_set[wire::kShaSize] = {};
        std::uint32_t snap_mission = 0;
        std::uint8_t want_mask = 0;
        std::uint32_t want_total = 0;
        unsigned cur_file = 0;
        std::uint32_t cur_off = 0;
        std::uint64_t sent_bytes = 0;
        std::uint64_t phase_at = 0, retry_at = 0;
        std::uint64_t progress_at = 0;    // sending: the WANT or the last queued CHUNK (kChunkStallMs)
        bool stop = false;                // no more OFFERs of stop_set this session (a DECLINE)
        std::uint8_t stop_set[wire::kShaSize] = {};
        // Guest.
        GuestPhase guest = GuestPhase::idle;
        bool declined = false;            // Lua op 3 / F9: for the rest of the process
        bool need_known = false, need_logged = false;
        std::uint8_t need = 0;
        NeedWhy need_why = NeedWhy::off;
        unsigned file = 0;
        std::uint32_t file_off = 0;
        link::Sha256 file_hash{};
        std::uint64_t recv_bytes = 0, recv_at = 0, last_chunk = 0;
        unsigned progress_logged = 0;     // 25 / 50 / 75 / 100 steps already logged
        bool stage_ok = false;
        std::uint32_t stage_xfer = 0;
        bool request_written = false, result_sent = false, result_retry = false;
        std::uint64_t countdown_at = 0, lost_at = 0;
        bool title_lost = false;
        unsigned g2_left = 0;
        bool exit_posted = false, exit_window = false, exit_slow = false;
        std::uint64_t exit_at = 0;
        // P4: RESULT exiting queued, post_exit not yet (kExitFlushMs).
        bool exit_pending = false, exit_unsent = false;
        std::uint64_t exit_req_at = 0;
        bool g12_toasted = false;         // G12 once per link session
        bool applied = false;
    };

    Action* Emit(ActionKind kind) noexcept;
    void Log(const char* format, ...) noexcept;
    void PostToast(save_handoff_state::Toast code, std::uint32_t a, std::uint32_t b) noexcept;
    bool Send(const wire::Message& message) noexcept;
    void SendSmall(wire::Kind kind, std::uint32_t xfer, std::uint8_t value, std::uint8_t reason) noexcept;
    void SetToken(save_handoff_state::Token token, std::uint8_t why) noexcept;
    void Flush() noexcept;
    void UtcNow(char (&out)[kUtcCapacity]) const noexcept;
    std::uint32_t NextXfer() noexcept;
    bool Fresh(std::uint64_t now_ms, std::uint64_t limit_ms) const noexcept;
    std::uint32_t OwnBound() const noexcept;
    wire::State BuildState(std::uint64_t now_ms) const noexcept;
    void Bind(const std::uint8_t (&set)[wire::kShaSize], std::uint32_t mission, Source source) noexcept;
    void TickState(std::uint64_t now_ms) noexcept;
    void TickHash(std::uint64_t now_ms) noexcept;
    void CountOffer(const std::uint8_t (&set)[wire::kShaSize]) noexcept;
    unsigned OffersFor(const std::uint8_t (&set)[wire::kShaSize]) const noexcept;
    // Host.
    void HostTick(std::uint64_t now_ms, unsigned queued) noexcept;
    bool GuestWants() const noexcept;
    void HostRest(std::uint64_t now_ms) noexcept;
    void StartOffer(std::uint64_t now_ms) noexcept;
    void ReleaseSnapshot() noexcept;
    void HostFail(wire::Fail fail, wire::AbortReason abort, std::uint64_t now_ms) noexcept;
    void HostEnd(save_handoff_state::Token token, std::uint8_t why, bool stop) noexcept;
    void HostMessage(const wire::Message& message, std::uint64_t now_ms) noexcept;
    void RewindTo(std::uint8_t slot, std::uint32_t offset) noexcept;
    // Guest.
    void GuestTick(std::uint64_t now_ms, unsigned queued) noexcept;
    void GuestNeedUpdate(std::uint64_t now_ms) noexcept;
    void GuestRest(std::uint64_t now_ms) noexcept;
    void GuestMessage(const wire::Message& message, std::uint64_t now_ms) noexcept;
    void GuestOffer(const wire::Message& message, std::uint64_t now_ms) noexcept;
    void GuestChunk(const wire::Message& message, std::uint64_t now_ms) noexcept;
    void GuestEnd(const wire::Message& message, std::uint64_t now_ms) noexcept;
    void GuestDecline(wire::DeclineReason reason, std::uint32_t xfer) noexcept;
    void GuestFail(wire::Fail fail, wire::Kind tell, std::uint64_t now_ms) noexcept;
    void GuestDropPartial(const char* why) noexcept;
    void GuestAbortStage(wire::AbortReason reason, std::uint64_t now_ms) noexcept;
    void GuestCancelExit(wire::AbortReason reason, std::uint64_t now_ms) noexcept;
    void StageCommitted(std::uint64_t now_ms) noexcept;
    void DoExit(std::uint64_t now_ms) noexcept;

    Config config_{};
    save_handoff_state::View view_{};
    save_handoff_state::View published_{};
    bool published_once_ = false;
    Provenance provenance_{};
    Manifest manifest_{};
    Request request_{};
    Stats stats_{};
    Machine m_{};
    Action ring_[kActionCapacity]{};
    std::size_t head_ = 0, count_ = 0;
    ActionKind awaiting_ = ActionKind::none;  // the popped action whose OnStageDone is due
    std::uint32_t awaiting_xfer_ = 0;
};

}  // namespace mgscoop::save_handoff_core
