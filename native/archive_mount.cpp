// Project-authored bounded startup adapter. ABI/address informed by tpp-mod
// 301bf797b7f8032941b21ed56a3570338de6ead4 (GPLv3), independently decoded and
// naturally observed: docs/archive-mount-observation.md. No upstream body copied.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <cstdint>
#include <cstring>
#include "mgscoop/archive_mount.hpp"

namespace {
constexpr std::size_t mount_rva = 0x48e20, mount_size = 0x16c;
constexpr char body_digest[] = "dc556b49319c37495c86d14dbb6eb93f22251aa4eaa4fb7a54deae4bd0fffea7";
constexpr char archive_digest[] = "87bbe82bb1e907cf405200155da2794bf7e1f5a3d45bf66426a38cdd31a91f96";
constexpr DWORD archive_size = 16333824;
void* mount_handle = nullptr;
volatile LONG attempted = 0;
char archive_path[32768]{};
constexpr char mount_name[] = "mgs_coop_player_runtime_v1";

bool DigestMatches(const unsigned char* digest, const char* expected) noexcept {
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
        if (hex[digest[i] >> 4] != expected[i*2] || hex[digest[i] & 15] != expected[i*2+1]) return false;
    return true;
}
bool HashMatches(HANDLE file, const void* bytes, ULONG size, const char* expected) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{}, buffer[65536]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (file == INVALID_HANDLE_VALUE) {
        if (ok) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), size, 0) >= 0;
    } else {
        DWORD remaining = size;
        while (ok && remaining) {
            DWORD read = 0;
            const DWORD count = remaining < sizeof(buffer) ? remaining : static_cast<DWORD>(sizeof(buffer));
            ok = ReadFile(file, buffer, count, &read, nullptr) && read == count;
            if (ok) ok = BCryptHashData(hash, buffer, read, 0) >= 0;
            remaining -= read;
        }
    }
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok && DigestMatches(digest, expected);
}
bool CodeMatches(void* base, std::size_t span) noexcept {
    if (!base || mount_rva > span || mount_size > span-mount_rva) return false;
    const auto* target = static_cast<const unsigned char*>(base)+mount_rva;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(target, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) ||
        reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(info.BaseAddress) + mount_size > info.RegionSize)
        return false;
    return HashMatches(INVALID_HANDLE_VALUE, target, static_cast<ULONG>(mount_size), body_digest);
}
}

bool MountPlayerArchive(void* base, std::size_t span, const wchar_t* directory,
                        void (*log)(const char*) noexcept) noexcept {
    // Called by the existing once-only accepted-build startup path. Never retry
    // a partial engine operation, including another call in this same process.
    if (!log || !directory || InterlockedCompareExchange(&attempted, 1, 0) != 0) return false;
    if (!CodeMatches(base, span)) { log("archive_mount=refused; reason=code_gate\r\n"); return false; }
    wchar_t path[32768]{};
    if (FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\mgs-coop\\mgs-coop-player-runtime.dat", directory))) return false;
    // Refuse lossy filesystem conversion. Current tested path is entirely ASCII.
    unsigned i = 0;
    for (; path[i] && i < ARRAYSIZE(archive_path)-1; ++i) {
        if (path[i] > 127) { log("archive_mount=refused; reason=non_ascii_path\r\n"); return false; }
        archive_path[i] = path[i] == L'\\' ? '/' : static_cast<char>(path[i]);
    }
    archive_path[i] = 0;
    // Deny writes while fingerprinting, then close before the engine opens it.
    // Do not impose our fingerprinting handle's sharing mode on the engine.
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER size{};
    const bool valid = file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size) && size.QuadPart == archive_size &&
        HashMatches(file, nullptr, archive_size, archive_digest);
    if (!valid) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        log("archive_mount=refused; reason=missing_or_changed_archive\r\n"); return false;
    }
    CloseHandle(file);
    char line[256]{};
    StringCchPrintfA(line, ARRAYSIZE(line), "archive_mount=calling; thread=%lu name=%s tag=0 flags=33 lifetime=process\r\n", GetCurrentThreadId(), mount_name);
    log(line);
    using Create = void* (*)(const char*, const char*, std::int64_t, std::uint32_t);
    const auto create = reinterpret_cast<Create>(static_cast<unsigned char*>(base)+mount_rva);
    // At 0x48ff0, bit 0x20 selects the verified QAR reader (0x5a1c0).
    // Bit 0x01 selects the 0x49a20 sorted search-list registration branch.
    // Natural chunk0 flags 0x34 opened the stream but keyed registration did
    // not resolve our unique script. Flags 0/1 did not create a stream at all.
    // No I/O setter or unmount helper is called; keep initial I/O configuration.
    mount_handle = create(mount_name, archive_path, 0, 0x21);
    const auto* fields = static_cast<const unsigned char*>(mount_handle);
    const auto* stream = fields ? *reinterpret_cast<void* const*>(fields+0x18) : nullptr;
    const auto kind = fields ? *reinterpret_cast<const std::uint32_t*>(fields+0x10) : 0;
    const bool ready = mount_handle && kind == 1 && stream;
    StringCchPrintfA(line, ARRAYSIZE(line), "archive_mount=returned; thread=%lu handle=%p kind=%u stream=%p success=%d; actor_packages_selected=0\r\n",
                     GetCurrentThreadId(), mount_handle, kind, stream, ready ? 1 : 0);
    log(line);
    return ready;
}
