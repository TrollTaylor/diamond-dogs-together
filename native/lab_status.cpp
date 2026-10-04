// Lab observability status formatting (docs/lab-protocol.md, "Status outputs").
//
// Pure, portable formatting plus a Win32 atomic-replacement helper. The pure
// functions never allocate and never touch the filesystem, so the loader keeps
// its live status in a constant-initialized POD global and these routines just
// render it. The atomic helper writes `<path>.tmp` then MoveFileExW-replaces.
#include "mgscoop/lab_status.hpp"
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace mgscoop::lab {
namespace {
// Appends a NUL-terminated literal, returns false on overflow.
bool Append(char* out, std::size_t capacity, std::size_t& length, const char* text) noexcept {
    for (std::size_t i = 0; text[i]; ++i) {
        if (length + 1 >= capacity) return false;
        out[length++] = text[i];
    }
    if (length < capacity) out[length] = 0;
    return true;
}
// Appends an unsigned decimal.
bool AppendU64(char* out, std::size_t capacity, std::size_t& length, std::uint64_t value) noexcept {
    char digits[20]{};
    std::size_t count = 0;
    do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    if (length + count >= capacity) return false;
    while (count) out[length++] = digits[--count];
    if (length < capacity) out[length] = 0;
    return true;
}
// Appends a signed decimal.
bool AppendI32(char* out, std::size_t capacity, std::size_t& length, std::int32_t value) noexcept {
    if (value < 0) {
        if (length + 1 >= capacity) return false;
        out[length++] = '-';
        return AppendU64(out, capacity, length, static_cast<std::uint64_t>(-static_cast<std::int64_t>(value)));
    }
    return AppendU64(out, capacity, length, static_cast<std::uint64_t>(value));
}
bool AppendField(char* out, std::size_t capacity, std::size_t& length,
                 const char* key, const char* quoted_value) noexcept {
    return Append(out, capacity, length, key) &&
           AppendJsonString(out, capacity, length, quoted_value);
}
} // namespace

const char* RoleName(Role value) noexcept {
    switch (value) {
    case Role::host: return "host";
    case Role::guest: return "guest";
    default: return "none";
    }
}
const char* ModeName(Mode value) noexcept {
    switch (value) {
    case Mode::host_v3: return "host_v3";
    case Mode::client_v1: return "client_v1";
    case Mode::client_check_v1: return "client_check_v1";
    default: return "off";
    }
}
const char* SessionStatusName(SessionStatus value) noexcept {
    switch (value) {
    case SessionStatus::host_ready: return "host_ready";
    case SessionStatus::host_peer_present: return "host_peer_present";
    case SessionStatus::client_requested: return "client_requested";
    case SessionStatus::client_established: return "client_established";
    case SessionStatus::session_error: return "session_error";
    case SessionStatus::closed: return "closed";
    default: return "none";
    }
}
const char* JoinStatusName(JoinStatus value) noexcept {
    switch (value) {
    case JoinStatus::armed: return "armed";
    case JoinStatus::requested: return "requested";
    case JoinStatus::refused: return "refused";
    case JoinStatus::failed: return "failed";
    case JoinStatus::established: return "established";
    default: return "idle";
    }
}

bool SameContent(const Status& a, const Status& b) noexcept {
    return a.pid == b.pid && a.role == b.role && a.mode == b.mode && a.mission == b.mission &&
           a.session == b.session && a.members == b.members && a.local_index == b.local_index &&
           a.join == b.join && a.join_attempts == b.join_attempts &&
           std::memcmp(a.last_refusal, b.last_refusal, kRefusalCapacity) == 0;
}

std::uint32_t MembersFor(SessionStatus status) noexcept {
    switch (status) {
    case SessionStatus::host_peer_present:
    case SessionStatus::client_established: return 2;
    case SessionStatus::host_ready:
    case SessionStatus::client_requested: return 1;
    default: return 0;
    }
}

JoinStatus JoinStatusForRefusal(const char* reason) noexcept {
    return reason && std::strcmp(reason, kRequestNotStarted) == 0 ? JoinStatus::failed : JoinStatus::refused;
}

JoinStatus JoinAfterSession(JoinStatus join, SessionStatus session) noexcept {
    if (session == SessionStatus::client_established) return JoinStatus::established;
    if (session == SessionStatus::session_error &&
        (join == JoinStatus::requested || join == JoinStatus::established)) return JoinStatus::failed;
    return join;
}

SessionStatus SessionAfterRelease(SessionStatus previous) noexcept {
    return previous == SessionStatus::none ? SessionStatus::none : SessionStatus::closed;
}

void SetRefusal(Status& status, const char* reason) noexcept {
    std::memset(status.last_refusal, 0, kRefusalCapacity);
    if (!reason) return;
    for (std::size_t i = 0; i + 1 < kRefusalCapacity && reason[i]; ++i)
        status.last_refusal[i] = reason[i];
}

bool AppendJsonString(char* out, std::size_t capacity, std::size_t& length, const char* text) noexcept {
    const std::size_t start = length;
    const auto put = [&](char c) noexcept {
        if (length + 1 >= capacity) return false;
        out[length++] = c;
        return true;
    };
    if (!put('"')) { length = start; return false; }
    for (std::size_t i = 0; text && text[i]; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        bool ok = true;
        if (c == '"') ok = put('\\') && put('"');
        else if (c == '\\') ok = put('\\') && put('\\');
        else if (c == '\n') ok = put('\\') && put('n');
        else if (c == '\r') ok = put('\\') && put('r');
        else if (c == '\t') ok = put('\\') && put('t');
        else if (c < 0x20) {
            static const char hex[] = "0123456789abcdef";
            ok = put('\\') && put('u') && put('0') && put('0') &&
                 put(hex[(c >> 4) & 0xf]) && put(hex[c & 0xf]);
        } else if (c >= 0x80) ok = put('?');
        else ok = put(static_cast<char>(c));
        if (!ok) { length = start; return false; }
    }
    if (!put('"')) { length = start; return false; }
    if (length < capacity) out[length] = 0;
    return true;
}

bool FormatStatusJson(const Status& status, char* out, std::size_t capacity, std::size_t& length) noexcept {
    if (!out || capacity == 0) return false;
    length = 0;
    out[0] = 0;
    const bool ok =
        Append(out, capacity, length, "{\"version\":1,\"pid\":") &&
        AppendU64(out, capacity, length, status.pid) &&
        AppendField(out, capacity, length, ",\"role\":", RoleName(status.role)) &&
        AppendField(out, capacity, length, ",\"mode\":", ModeName(status.mode)) &&
        Append(out, capacity, length, ",\"mission\":") &&
        AppendU64(out, capacity, length, status.mission) &&
        AppendField(out, capacity, length, ",\"session_status\":", SessionStatusName(status.session)) &&
        Append(out, capacity, length, ",\"members\":") &&
        AppendU64(out, capacity, length, status.members) &&
        Append(out, capacity, length, ",\"local_index\":") &&
        AppendI32(out, capacity, length, status.local_index) &&
        AppendField(out, capacity, length, ",\"join\":", JoinStatusName(status.join)) &&
        Append(out, capacity, length, ",\"join_attempts\":") &&
        AppendU64(out, capacity, length, status.join_attempts) &&
        Append(out, capacity, length, ",\"last_refusal\":") &&
        AppendJsonString(out, capacity, length, status.last_refusal) &&
        Append(out, capacity, length, ",\"updated_tick_ms\":") &&
        AppendU64(out, capacity, length, status.updated_tick_ms) &&
        Append(out, capacity, length, "}\n");
    if (!ok) { length = 0; out[0] = 0; return false; }
    return true;
}

bool SessionStatusFromLiteral(const char* literal, SessionStatus& out) noexcept {
    if (!literal) return false;
    // Accept either the bare status word or the Lua receipt "return '<word>'".
    const char* word = literal;
    const char* prefix = "return '";
    std::size_t p = 0;
    for (; prefix[p]; ++p) if (literal[p] != prefix[p]) { p = 0; break; }
    std::size_t word_len = 0;
    if (p) {
        word = literal + p;
        while (word[word_len] && word[word_len] != '\'') ++word_len;
    } else {
        while (word[word_len]) ++word_len;
    }
    const auto is = [&](const char* name) noexcept {
        std::size_t i = 0;
        for (; name[i]; ++i) if (i >= word_len || word[i] != name[i]) return false;
        return i == word_len;
    };
    if (is("host_ready")) { out = SessionStatus::host_ready; return true; }
    if (is("host_peer_present")) { out = SessionStatus::host_peer_present; return true; }
    if (is("client_connecting") || is("client_requested")) { out = SessionStatus::client_requested; return true; }
    if (is("client_established")) { out = SessionStatus::client_established; return true; }
    if (is("session_error")) { out = SessionStatus::session_error; return true; }
    if (is("closed")) { out = SessionStatus::closed; return true; }
    // A session exists but cannot be joined: the lab status is "none".
    if (is("host_not_accepting")) { out = SessionStatus::none; return true; }
    // 'unavailable', empty and anything unknown: keep the previous status.
    return false;
}

bool FormatSessionStatusLine(SessionStatus status, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return false;
    out[0] = 0;
    std::size_t length = 0;
    const bool ok = Append(out, capacity, length, "session_status=") &&
                    Append(out, capacity, length, SessionStatusName(status)) &&
                    Append(out, capacity, length, "\r\n");
    if (!ok) { out[0] = 0; return false; }
    return true;
}

bool FormatSteamIdLine(std::uint64_t id, char* out, std::size_t capacity, std::size_t& length) noexcept {
    if (!out || capacity == 0) return false;
    length = 0;
    out[0] = 0;
    const bool ok = AppendU64(out, capacity, length, id) && Append(out, capacity, length, "\n");
    if (!ok) { length = 0; out[0] = 0; return false; }
    return true;
}

bool MakeTemporaryPath(const wchar_t* path, wchar_t* out, std::size_t capacity) noexcept {
    if (!path || !out || capacity == 0) return false;
    std::size_t n = 0;
    while (path[n]) ++n;
    const wchar_t suffix[] = L".tmp";
    constexpr std::size_t suffix_len = 4;
    if (n + suffix_len + 1 > capacity) return false;
    for (std::size_t i = 0; i < n; ++i) out[i] = path[i];
    for (std::size_t i = 0; i < suffix_len; ++i) out[n + i] = suffix[i];
    out[n + suffix_len] = 0;
    return true;
}

bool WriteFileAtomic(const AtomicWriteOps& ops, const wchar_t* path, const void* data, std::size_t size) noexcept {
    if (!ops.create || !ops.write || !ops.close || !ops.replace || !ops.remove || !path || (!data && size)) return false;
    wchar_t temporary[kMaxAtomicPath]{};
    if (!MakeTemporaryPath(path, temporary, kMaxAtomicPath)) return false;
    void* file = ops.create(ops.context, temporary);
    if (!file) return false;
    bool ok = ops.write(ops.context, file, data, size);
    if (!ops.close(ops.context, file)) ok = false;
    if (ok && ops.replace(ops.context, temporary, path)) return true;
    ops.remove(ops.context, temporary);
    return false;
}

#ifdef _WIN32
namespace {
void* Win32Create(void*, const wchar_t* path) noexcept {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    return file == INVALID_HANDLE_VALUE ? nullptr : file;
}
bool Win32Write(void*, void* file, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t written_total = 0;
    while (written_total < size) {
        const DWORD chunk = size - written_total > 0x40000000u
            ? 0x40000000u : static_cast<DWORD>(size - written_total);
        DWORD written = 0;
        if (!WriteFile(file, bytes + written_total, chunk, &written, nullptr) || written != chunk) return false;
        written_total += written;
    }
    return true;
}
bool Win32Close(void*, void* file) noexcept { return CloseHandle(file) != 0; }
bool Win32Replace(void*, const wchar_t* from, const wchar_t* to) noexcept {
    return MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
}
bool Win32Remove(void*, const wchar_t* path) noexcept { return DeleteFileW(path) != 0; }
} // namespace

AtomicWriteOps Win32AtomicWriteOps() noexcept {
    AtomicWriteOps ops{};
    ops.context = nullptr;
    ops.create = &Win32Create;
    ops.write = &Win32Write;
    ops.close = &Win32Close;
    ops.replace = &Win32Replace;
    ops.remove = &Win32Remove;
    return ops;
}

bool WriteFileAtomic(const wchar_t* path, const void* data, std::size_t size) noexcept {
    const DWORD error = GetLastError();
    const bool ok = WriteFileAtomic(Win32AtomicWriteOps(), path, data, size);
    SetLastError(error);
    return ok;
}
#endif

} // namespace mgscoop::lab
