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
#include <cwchar>
#include "mgscoop/archive_mount.hpp"
#include "mgscoop/buddy_capacity_catalog.hpp"

namespace {
constexpr std::size_t mount_rva = 0x48e20, mount_size = 0x16c;
constexpr char body_digest[] = "dc556b49319c37495c86d14dbb6eb93f22251aa4eaa4fb7a54deae4bd0fffea7";
constexpr char archive_digest[] = "87bbe82bb1e907cf405200155da2794bf7e1f5a3d45bf66426a38cdd31a91f96";
constexpr DWORD archive_size = 16333824;
#if defined(MGS_LOCAL_GUN_PRELOAD_TEST)
constexpr char experimental_archive_digest[] = "2e314419b3e42794e5b89a253ad767d28a1e6834f88aae22df5d0dedfed66c9a";
constexpr DWORD experimental_archive_size = 16944128;
#endif
void* mount_handle = nullptr;
volatile LONG attempted = 0;
volatile LONG mounted_motion_count = 0;
volatile LONG mounted_buddy_capacity = 0;
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
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_BUDDY_CAPACITY_TEST)
template<std::size_t N> bool WorkerEnvironment(const wchar_t* name, wchar_t (&value)[N]) noexcept {
    const auto length = GetEnvironmentVariableW(name, value, static_cast<DWORD>(N));
    return length > 0 && length < N && value[length] == 0;
}
bool WorkerBuddyCapacityContext(const wchar_t* directory) noexcept {
    // Environment alone is insufficient: inspect the actual current thread
    // desktop and refuse the active input desktop. No UI/input mutation.
    wchar_t run[33]{}, role[8]{}, expected_desktop[64]{}, desktop[128]{}, input_name[128]{};
    wchar_t profile[64]{}, digest[65]{}, motions[8]{}, capacity[8]{}, game[32768]{};
    if (!WorkerEnvironment(L"MGS_WORKER_RUN_ID", run) || wcslen(run) != 32 ||
        !WorkerEnvironment(L"MGS_WORKER_ROLE", role) ||
        (wcscmp(role, L"host") && wcscmp(role, L"guest"))) return false;
    for (wchar_t c : run) if (c && !((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    if (FAILED(StringCchPrintfW(expected_desktop, ARRAYSIZE(expected_desktop), L"MGSWorker_%s", run))) return false;
    if (!WorkerEnvironment(L"MGS_WORKER_DESKTOP", desktop) || wcscmp(desktop, expected_desktop) ||
        !WorkerEnvironment(L"MGS_WORKER_RUNTIME_PROFILE", profile) || wcscmp(profile, L"buddy_capacity_two_v1") ||
        !WorkerEnvironment(L"MGS_WORKER_ARCHIVE_SHA256", digest) || wcslen(digest) != 64 ||
        _wcsicmp(digest, L"4b290cddc14f1146dce5a64a1008365d572b424b319874ef77e9496634d75ec9") ||
        !WorkerEnvironment(L"MGS_WORKER_MOTION_COUNT", motions) || wcslen(motions) != 2 || wcscmp(motions, L"21") ||
        !WorkerEnvironment(L"MGS_WORKER_BUDDY_CAPACITY", capacity) || wcslen(capacity) != 1 || wcscmp(capacity, L"2") ||
        !WorkerEnvironment(L"MGS_WORKER_GAME_DIR", game) || _wcsicmp(game, directory)) return false;
    DWORD needed = 0;
    if (!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &needed) ||
        needed == 0 || needed > sizeof(desktop) || wcscmp(desktop, expected_desktop)) return false;
    HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!input) return false;
    const bool visible = GetUserObjectInformationW(input, UOI_NAME, input_name, sizeof(input_name), &needed) != FALSE &&
        needed > 0 && needed <= sizeof(input_name) && wcscmp(input_name, expected_desktop) != 0;
    CloseDesktop(input);
    return visible;
}
#endif
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

unsigned MountedPlayerMotionCount() noexcept {
    return static_cast<unsigned>(InterlockedCompareExchange(&mounted_motion_count, 0, 0));
}

bool MountedBuddyCapacity() noexcept {
    return InterlockedCompareExchange(&mounted_buddy_capacity, 0, 0) == 1;
}

bool MountPlayerArchive(void* base, std::size_t span, const wchar_t* directory,
                        void (*log)(const char*) noexcept,
                        bool allow_experimental) noexcept {
#if !defined(MGS_LOCAL_GUN_PRELOAD_TEST) && !defined(MGS_LOCAL_BUDDY_CAPACITY_TEST)
    (void)allow_experimental;
#endif
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
    const bool size_read = file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &size);
    const char* expected_digest = archive_digest;
    DWORD expected_size = archive_size;
    bool experimental = false;
    bool buddy_capacity = false;
    if (size_read && size.QuadPart == static_cast<LONGLONG>(mgscoop::buddy_capacity::superseded_archive_bytes)) {
        CloseHandle(file);
        log("archive_mount=refused; reason=superseded_buddy_capacity; bytes=16659456; required_bytes=17501184\r\n");
        return false;
    }
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_BUDDY_CAPACITY_TEST)
    if (allow_experimental && size_read && size.QuadPart == static_cast<LONGLONG>(mgscoop::buddy_capacity::archive_bytes) &&
        WorkerBuddyCapacityContext(directory)) {
        expected_digest = mgscoop::buddy_capacity::archive_sha;
        expected_size = static_cast<DWORD>(mgscoop::buddy_capacity::archive_bytes);
        buddy_capacity = true;
    }
#endif
#if defined(MGS_LOCAL_GUN_PRELOAD_TEST)
    if (allow_experimental && size_read && size.QuadPart == experimental_archive_size) {
        expected_digest = experimental_archive_digest;
        expected_size = experimental_archive_size;
        experimental = true;
    }
#endif
    const bool valid = size_read && size.QuadPart == expected_size &&
        HashMatches(file, nullptr, expected_size, expected_digest);
    if (!valid) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        log("archive_mount=refused; reason=missing_or_changed_archive\r\n"); return false;
    }
    CloseHandle(file);
    log(buddy_capacity ? "archive_mount=verified; profile=buddy_capacity_two_v1; buddy_capacity=2; horse_fpkd=00,03,02_0,02_1,02_2,05,04; walker_fpkd=wg2_00; live_allocation=unverified\r\n" :
        experimental ? "archive_mount=verified; profile=local_gun_preload_test\r\n"
                     : "archive_mount=verified; profile=two_player_baseline\r\n");
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
    // This is the verified resident GANI catalogue allowance, not QAR entry count.
    // Eight turns already live in the unchanged game resident MTAR; no receiver preload is needed.
    if (ready) {
        InterlockedExchange(&mounted_motion_count, experimental ? 31 : 21);
        InterlockedExchange(&mounted_buddy_capacity, buddy_capacity ? 1 : 0);
    }
    return ready;
}
