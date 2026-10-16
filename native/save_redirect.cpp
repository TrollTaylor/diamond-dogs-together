// Original co-op lab save-folder and Steam Cloud isolation.
#include "mgscoop/save_redirect.hpp"

#include "mgscoop/steam_identity.hpp"

#include <cstring>

namespace mgscoop::lab::save {
namespace {
std::size_t base_len(const char* s) noexcept {
    std::size_t n = 0;
    while (s[n]) ++n;
    while (n && (s[n - 1] == '\\' || s[n - 1] == '/')) --n;  // drop trailing separators
    return n;
}
bool append(char* out, std::size_t cap, std::size_t& at, const char* text) noexcept {
    for (std::size_t i = 0; text[i]; ++i) {
        if (at + 1 >= cap) return false;
        out[at++] = text[i];
    }
    out[at] = 0;
    return true;
}
}  // namespace

bool is_safe_remote_name(const char* name) noexcept {
    if (!name || !*name) return false;
    for (std::size_t i = 0; name[i]; ++i) {
        const char c = name[i];
        if (c == '\\' || c == '/' || c == ':') return false;
    }
    // Reject "." and ".." whole-name references.
    if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0))) return false;
    return true;
}

bool save_dir_fits(const char* save_dir) noexcept {
    if (!save_dir || !*save_dir) return false;
    const std::size_t base = base_len(save_dir);
    return base != 0 && base <= kMaxSaveDirChars;
}

bool format_user_data_folder(const char* save_dir, char* buf, int cub) noexcept {
    if (!save_dir || !*save_dir || !buf || cub <= 0) return false;
    const std::size_t base = base_len(save_dir);
    if (!base) return false;
    const std::size_t cap = static_cast<std::size_t>(cub);
    std::size_t at = 0;
    for (std::size_t i = 0; i < base; ++i) {
        if (at + 1 >= cap) return false;
        buf[at++] = save_dir[i];
    }
    buf[at] = 0;
    return append(buf, cap, at, "\\287700\\local");
}

bool format_remote_path(const char* save_dir, const char* name, char* out, std::size_t out_size) noexcept {
    if (!save_dir || !*save_dir || !out || !out_size || !is_safe_remote_name(name)) return false;
    const std::size_t base = base_len(save_dir);
    if (!base) return false;
    std::size_t at = 0;
    for (std::size_t i = 0; i < base; ++i) {
        if (at + 1 >= out_size) return false;
        out[at++] = save_dir[i];
    }
    out[at] = 0;
    if (!append(out, out_size, at, "\\remote\\")) return false;
    return append(out, out_size, at, name);
}

bool format_local_path(const char* save_dir, const char* name, char* out, std::size_t out_size) noexcept {
    if (out && out_size) out[0] = 0;
    if (!save_dir || !*save_dir || !out || !out_size || !is_safe_remote_name(name)) return false;
    const std::size_t base = base_len(save_dir);
    if (!base) return false;
    std::size_t at = 0;
    for (std::size_t i = 0; i < base; ++i) {
        if (at + 1 >= out_size) { out[0] = 0; return false; }
        out[at++] = save_dir[i];
    }
    out[at] = 0;
    if (!append(out, out_size, at, "\\287700\\local\\") || !append(out, out_size, at, name)) { out[0] = 0; return false; }
    return true;
}

bool format_handoff_dir(const char* save_dir, char* out, std::size_t out_size) noexcept {
    if (out && out_size) out[0] = 0;
    if (!save_dir || !*save_dir || !out || !out_size) return false;
    const std::size_t base = base_len(save_dir);
    if (!base) return false;
    // The folder plus the tooling's longest staged name must stay below the limit.
    if (base + (sizeof(kHandoffSuffix) - 1) + kHandoffLongestTail >= kHandoffPathLimit) return false;
    std::size_t at = 0;
    for (std::size_t i = 0; i < base; ++i) {
        if (at + 1 >= out_size) { out[0] = 0; return false; }
        out[at++] = save_dir[i];
    }
    out[at] = 0;
    if (!append(out, out_size, at, kHandoffSuffix)) { out[0] = 0; return false; }
    return true;
}

}  // namespace mgscoop::lab::save

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace mgscoop::lab::save {
namespace {
char g_save_dir[260]{};
bool g_configured = false;
SRWLOCK g_lock = SRWLOCK_INIT;
// The redirected remote slot I/O (save handoff P2-6): RS_FileWrite and
// RS_FileDelete hold it exclusive, RS_FileRead and read_remote_snapshot
// shared. A leaf lock: nothing else is taken while it is held.
SRWLOCK g_io_lock = SRWLOCK_INIT;
struct IoShared {
    IoShared() noexcept { AcquireSRWLockShared(&g_io_lock); }
    ~IoShared() { ReleaseSRWLockShared(&g_io_lock); }
    IoShared(const IoShared&) = delete;
    IoShared& operator=(const IoShared&) = delete;
};
struct IoExclusive {
    IoExclusive() noexcept { AcquireSRWLockExclusive(&g_io_lock); }
    ~IoExclusive() { ReleaseSRWLockExclusive(&g_io_lock); }
    IoExclusive(const IoExclusive&) = delete;
    IoExclusive& operator=(const IoExclusive&) = delete;
};
void* g_storage_real = nullptr;
void* g_storage_proxy = nullptr;
// GetFileNameAndSize returns a pointer valid until the next call; the game runs
// the save path on one thread, and the lock serializes the enumerator anyway.
char g_enum_name[260]{};

bool to_wide(const char* utf8, wchar_t* out, int out_chars) noexcept {
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, out_chars) > 0;
}
void make_dir_tree(const wchar_t* path) noexcept {
    wchar_t partial[MAX_PATH * 2]{};
    std::size_t len = 0;
    for (; path[len] && len + 1 < ARRAYSIZE(partial); ++len) {
        partial[len] = path[len];
        if ((path[len] == L'\\' || path[len] == L'/') && len > 2) {
            partial[len] = 0;
            CreateDirectoryW(partial, nullptr);
            partial[len] = L'\\';
        }
    }
    partial[len] = 0;
    CreateDirectoryW(partial, nullptr);
}
bool remote_wide(const char* name, wchar_t* out, int out_chars) noexcept {
    char ansi[520]{};
    if (!format_remote_path(g_save_dir, name, ansi, sizeof(ansi))) return false;
    return to_wide(ansi, out, out_chars);
}
bool remote_dir_pattern(wchar_t* out, int out_chars) noexcept {
    char ansi[520]{};
    if (!format_remote_path(g_save_dir, "star", ansi, sizeof(ansi))) return false;
    std::size_t n = std::strlen(ansi);
    ansi[n - 4] = '*';  // replace "star" with "*"
    ansi[n - 3] = 0;
    return to_wide(ansi, out, out_chars);
}

void* real_of(void* self) noexcept { return static_cast<ProxyHeader*>(self)->real; }
template <typename Fn>
Fn real_slot(void* self, unsigned slot) noexcept {
    void* real = real_of(self);
    auto* const* vtable = *static_cast<void* const* const*>(real);
    return reinterpret_cast<Fn>(vtable[slot]);
}

// ----- ISteamRemoteStorage012 overrides -----
bool MGSCOOP_STEAM_CALL RS_FileWrite(void* /*self*/, const char* name, const void* data, std::int32_t cub) noexcept {
    wchar_t path[MAX_PATH * 2]{};
    if (cub < 0 || (cub > 0 && !data) || !remote_wide(name, path, ARRAYSIZE(path))) return false;
    const IoExclusive io;  // a handoff snapshot read finishes first (never a sharing violation)
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = (cub == 0) ||
                    (WriteFile(f, data, static_cast<DWORD>(cub), &written, nullptr) && written == static_cast<DWORD>(cub));
    CloseHandle(f);
    return ok;
}
std::int32_t MGSCOOP_STEAM_CALL RS_FileRead(void* /*self*/, const char* name, void* data, std::int32_t cub) noexcept {
    wchar_t path[MAX_PATH * 2]{};
    if (cub <= 0 || !data || !remote_wide(name, path, ARRAYSIZE(path))) return 0;
    const IoShared io;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD read = 0;
    const bool ok = ReadFile(f, data, static_cast<DWORD>(cub), &read, nullptr) != FALSE;
    CloseHandle(f);
    return ok ? static_cast<std::int32_t>(read) : 0;
}
bool MGSCOOP_STEAM_CALL RS_FileForget(void* /*self*/, const char* name) noexcept {
    return is_safe_remote_name(name);  // local-only lab: nothing to un-cache
}
bool MGSCOOP_STEAM_CALL RS_FileDelete(void* /*self*/, const char* name) noexcept {
    wchar_t path[MAX_PATH * 2]{};
    if (!remote_wide(name, path, ARRAYSIZE(path))) return false;
    const IoExclusive io;
    return DeleteFileW(path) != FALSE;
}
bool MGSCOOP_STEAM_CALL RS_FileExists(void* /*self*/, const char* name) noexcept {
    wchar_t path[MAX_PATH * 2]{};
    if (!remote_wide(name, path, ARRAYSIZE(path))) return false;
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}
std::int32_t MGSCOOP_STEAM_CALL RS_GetFileSize(void* /*self*/, const char* name) noexcept {
    wchar_t path[MAX_PATH * 2]{};
    if (!remote_wide(name, path, ARRAYSIZE(path))) return 0;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info)) return 0;
    return info.nFileSizeHigh ? 0x7fffffff : static_cast<std::int32_t>(info.nFileSizeLow);
}
std::int32_t MGSCOOP_STEAM_CALL RS_GetFileCount(void* /*self*/) noexcept {
    wchar_t pattern[MAX_PATH * 2]{};
    if (!remote_dir_pattern(pattern, ARRAYSIZE(pattern))) return 0;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    std::int32_t count = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++count;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}
const char* MGSCOOP_STEAM_CALL RS_GetFileNameAndSize(void* /*self*/, int index, std::int32_t* size) noexcept {
    if (size) *size = 0;
    wchar_t pattern[MAX_PATH * 2]{};
    if (index < 0 || !remote_dir_pattern(pattern, ARRAYSIZE(pattern))) return "";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    const char* result = "";
    int i = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (i == index) {
            if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, g_enum_name, sizeof(g_enum_name), nullptr, nullptr) > 0) {
                if (size) *size = fd.nFileSizeHigh ? 0x7fffffff : static_cast<std::int32_t>(fd.nFileSizeLow);
                result = g_enum_name;
            }
            break;
        }
        ++i;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}
}  // namespace

void configure(const char* save_dir) noexcept {
    if (!save_dir || !*save_dir) return;
    AcquireSRWLockExclusive(&g_lock);
    std::size_t i = 0;
    for (; save_dir[i] && i + 1 < sizeof(g_save_dir); ++i) g_save_dir[i] = save_dir[i];
    g_save_dir[i] = 0;
    g_configured = true;
    char local[520]{};
    if (format_user_data_folder(g_save_dir, local, static_cast<int>(sizeof(local)))) {
        wchar_t wide[MAX_PATH * 2]{};
        if (to_wide(local, wide, ARRAYSIZE(wide))) make_dir_tree(wide);
    }
    char remote[520]{};
    if (format_remote_path(g_save_dir, "x", remote, sizeof(remote))) {
        std::size_t n = std::strlen(remote);
        if (n >= 2) remote[n - 2] = 0;  // trim "\x" to the remote directory
        wchar_t wide[MAX_PATH * 2]{};
        if (to_wide(remote, wide, ARRAYSIZE(wide))) make_dir_tree(wide);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool configured() noexcept { return g_configured; }

bool current_save_dir(char* out, std::size_t out_size) noexcept {
    if (!out || !out_size) return false;
    out[0] = 0;
    bool ok = false;
    AcquireSRWLockShared(&g_lock);
    if (g_configured && g_save_dir[0]) {
        std::size_t n = 0;
        while (g_save_dir[n]) ++n;
        if (n < out_size) {
            for (std::size_t i = 0; i <= n; ++i) out[i] = g_save_dir[i];
            ok = true;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

bool read_remote_snapshot(const char* name, void* buf, std::size_t cap, std::size_t& size) noexcept {
    size = 0;
    if (!buf || !cap || !g_configured || !is_safe_remote_name(name) || cap > 0x7FFFFFFFu) return false;
    wchar_t path[MAX_PATH * 2]{};
    if (!remote_wide(name, path, ARRAYSIZE(path))) return false;
    const DWORD error = GetLastError();
    bool ok = false;
    {
        // Shared, like RS_FileRead: the game's exclusive RS_FileWrite waits for
        // this read instead of failing on the open handle.
        const IoShared io;
        HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER length{};
            if (GetFileSizeEx(f, &length) && length.QuadPart >= 0 && static_cast<unsigned long long>(length.QuadPart) <= cap) {
                const auto want = static_cast<DWORD>(length.QuadPart);
                DWORD read = 0;
                // One byte more than the size: a file that grew is refused, not truncated.
                const DWORD ask = want < cap ? want + 1 : want;
                if (ReadFile(f, buf, ask, &read, nullptr) && read == want) {
                    size = read;
                    ok = true;
                }
            }
            CloseHandle(f);
        }
    }
    SetLastError(error);
    return ok;
}

void* wrap_remote_storage(void* real) noexcept {
    if (!real || !g_configured) return real;
    AcquireSRWLockExclusive(&g_lock);
    if (real == g_storage_real && g_storage_proxy) {
        void* cached = g_storage_proxy;
        ReleaseSRWLockExclusive(&g_lock);
        return cached;
    }
    ProxyOverride overrides[] = {
        {steam::storage_slot::FileWrite, reinterpret_cast<const void*>(&RS_FileWrite)},
        {steam::storage_slot::FileRead, reinterpret_cast<const void*>(&RS_FileRead)},
        {steam::storage_slot::FileForget, reinterpret_cast<const void*>(&RS_FileForget)},
        {steam::storage_slot::FileDelete, reinterpret_cast<const void*>(&RS_FileDelete)},
        {steam::storage_slot::FileExists, reinterpret_cast<const void*>(&RS_FileExists)},
        {steam::storage_slot::GetFileSize, reinterpret_cast<const void*>(&RS_GetFileSize)},
        {steam::storage_slot::GetFileCount, reinterpret_cast<const void*>(&RS_GetFileCount)},
        {steam::storage_slot::GetFileNameAndSize, reinterpret_cast<const void*>(&RS_GetFileNameAndSize)},
    };
    void* proxy = build_forwarding_proxy(real, steam::storage_slot::kCount, overrides,
                                         static_cast<unsigned>(ARRAYSIZE(overrides)), nullptr);
    if (proxy) {
        g_storage_real = real;
        g_storage_proxy = proxy;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return proxy ? proxy : real;
}

void reset_for_test() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_save_dir[0] = 0;
    g_configured = false;
    g_storage_real = g_storage_proxy = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace mgscoop::lab::save
#endif  // _WIN32
