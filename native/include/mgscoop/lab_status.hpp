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

// Appends a quoted JSON string. Escapes '"', '\\' and every byte below 0x20;
// bytes >= 0x80 are replaced by '?' so the file is always valid ASCII JSON.
// On failure (capacity) returns false and leaves `length` unchanged.
bool AppendJsonString(char* out, std::size_t capacity, std::size_t& length, const char* text) noexcept;

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
