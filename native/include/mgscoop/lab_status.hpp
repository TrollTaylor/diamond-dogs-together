#pragma once
// Lab observability contract (docs/lab-protocol.md, "Status outputs").
//
// Pure formatting/mapping helpers plus an atomic file replacement routine.
// The formatting functions are portable and unit tested; they never allocate,
// so the loader can keep its status in constant-initialized POD globals.
#include <cstddef>
#include <cstdint>

namespace mgscoop::lab {

enum class Role : std::uint8_t { none, host, guest };
enum class Mode : std::uint8_t { off, host_v3, client_v1, client_check_v1 };
enum class SessionStatus : std::uint8_t {
    none, host_ready, host_peer_present, client_requested, client_established, session_error, closed
};
enum class JoinStatus : std::uint8_t { idle, armed, requested, refused, failed, established };

// Protocol spellings. Unknown enum values map to the first vocabulary entry.
const char* RoleName(Role value) noexcept;
const char* ModeName(Mode value) noexcept;
const char* SessionStatusName(SessionStatus value) noexcept;
const char* JoinStatusName(JoinStatus value) noexcept;

constexpr std::size_t kRefusalCapacity = 48;
struct Status {
    std::uint32_t pid = 0;
    Role role = Role::none;
    Mode mode = Mode::off;
    std::uint32_t mission = 0;
    SessionStatus session = SessionStatus::none;
    std::uint32_t members = 0;
    std::int32_t local_index = -1;
    JoinStatus join = JoinStatus::idle;
    std::uint32_t join_attempts = 0;
    char last_refusal[kRefusalCapacity]{};
    std::uint64_t updated_tick_ms = 0;
};

// Every field except updated_tick_ms.
bool SameContent(const Status& a, const Status& b) noexcept;
// Bounded copy; null clears. Truncates to kRefusalCapacity-1 bytes.
void SetRefusal(Status& status, const char* reason) noexcept;

// Informational member count implied by a session status (not a roster read).
std::uint32_t MembersFor(SessionStatus status) noexcept;

// Refusal reason recorded when a native client request ran but returned false.
constexpr const char* kRequestNotStarted = "request_not_started";
// A join that never reached the native request (a gate or pre-request refusal)
// is "refused"; a native request that returned false is "failed".
JoinStatus JoinStatusForRefusal(const char* reason) noexcept;
// Join progress implied by a newly observed session status: an established
// client session means the join is established; a session error fails a
// requested or established join. Anything else leaves `join` unchanged.
JoinStatus JoinAfterSession(JoinStatus join, SessionStatus session) noexcept;
// Status once the game has released the owned session: "closed", unless
// nothing live was ever reported ("none" stays "none").
SessionStatus SessionAfterRelease(SessionStatus previous) noexcept;

// Throttles retries after a failed file write so a persistently failing path
// is retried at most once per interval instead of on every poll.
struct RetryThrottle {
    static constexpr std::uint64_t kIntervalMs = 1000;
    bool failed = false;
    std::uint64_t last_failure_ms = 0;
    bool Due(std::uint64_t now_ms) const noexcept { return !failed || now_ms - last_failure_ms >= kIntervalMs; }
    void Failed(std::uint64_t now_ms) noexcept { failed = true; last_failure_ms = now_ms; }
    void Succeeded() noexcept { failed = false; last_failure_ms = 0; }
};

// Remembers the last status that was *successfully* written. A failed write
// leaves it unchanged, so the next due poll writes again instead of treating a
// stale file as current.
struct PublishTracker {
    Status written{};
    bool written_valid = false;
    RetryThrottle retry{};
    bool NeedsWrite(const Status& current, std::uint64_t now_ms) const noexcept {
        if (written_valid && SameContent(written, current)) return false;
        return retry.Due(now_ms);
    }
    void Record(const Status& current, bool succeeded, std::uint64_t now_ms) noexcept {
        if (succeeded) { written = current; written_valid = true; retry.Succeeded(); }
        else retry.Failed(now_ms);
    }
};

// Appends a quoted JSON string. Escapes '"', '\\' and every byte below 0x20;
// bytes >= 0x80 are replaced by '?' so the file is always valid ASCII JSON.
// On failure (capacity) returns false and leaves `length` unchanged.
bool AppendJsonString(char* out, std::size_t capacity, std::size_t& length, const char* text) noexcept;

// link-status.json transport keys (docs/steam-online.md 11), appended before
// time_utc: "transport":"udp|steam","route":"relay|direct|unknown|none",
// "accepting":0|1,"steam":"ok|acquiring|off|<reason>", (with the trailing
// comma). On failure (capacity, or a name outside [a-z0-9_]) returns false
// and leaves `length` unchanged.
bool AppendLinkTransportJson(char* out, std::size_t capacity, std::size_t& length, const char* transport, const char* route,
                             bool accepting, const char* steam) noexcept;

constexpr std::size_t kStatusJsonCapacity = 768;
// Formats the version-1 document. `length` excludes the terminating NUL.
bool FormatStatusJson(const Status& status, char* out, std::size_t capacity, std::size_t& length) noexcept;

// Maps a ReadDynamiteSessionStatus Lua literal ("return 'host_ready'") or a
// bare status word to the protocol vocabulary. 'host_not_accepting' maps to
// none (a session exists but cannot be joined). Returns false for
// 'unavailable', null and unknown values: callers keep the previous status.
bool SessionStatusFromLiteral(const char* literal, SessionStatus& out) noexcept;

// "session_status=<value>\r\n"
bool FormatSessionStatusLine(SessionStatus status, char* out, std::size_t capacity) noexcept;

// Decimal SteamID64 followed by a single '\n' (host-steamid.txt).
bool FormatSteamIdLine(std::uint64_t id, char* out, std::size_t capacity, std::size_t& length) noexcept;

// Atomic replacement: write `<path>.tmp`, close it, then replace `path` with it
// (Win32: MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)). Any failure
// removes the temporary file and leaves the previous `path` content intact.
struct AtomicWriteOps {
    void* context = nullptr;
    void* (*create)(void* context, const wchar_t* path) noexcept = nullptr;  // truncate/create; nullptr = failure
    bool (*write)(void* context, void* file, const void* data, std::size_t size) noexcept = nullptr;
    bool (*close)(void* context, void* file) noexcept = nullptr;
    bool (*replace)(void* context, const wchar_t* from, const wchar_t* to) noexcept = nullptr;
    bool (*remove)(void* context, const wchar_t* path) noexcept = nullptr;
};
constexpr std::size_t kMaxAtomicPath = 1100;
bool MakeTemporaryPath(const wchar_t* path, wchar_t* out, std::size_t capacity) noexcept;
bool WriteFileAtomic(const AtomicWriteOps& ops, const wchar_t* path, const void* data, std::size_t size) noexcept;

#ifdef _WIN32
// CreateFileW(CREATE_ALWAYS) / WriteFile / CloseHandle /
// MoveFileExW(MOVEFILE_REPLACE_EXISTING) / DeleteFileW.
AtomicWriteOps Win32AtomicWriteOps() noexcept;
// Preserves the caller's last-error value.
bool WriteFileAtomic(const wchar_t* path, const void* data, std::size_t size) noexcept;
#endif

} // namespace mgscoop::lab
