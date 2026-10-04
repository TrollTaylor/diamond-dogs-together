// Adapted from Dynamite GetPatches Player2GameObjectImpl::UpdatePartsStatus
// ("skip some check that resets player to Snake from Quiet/Ocelot"), unknown321,
// c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
// Full terms: third_party/dynamite/LICENSE. Changes: 1.0.15.4 mapping
// (0x1409cc6c0 -> RVA 0x9cd370), complete-body hash gate, scoped activation,
// aligned two-byte interlocked exchange and exact restoration.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/player_type_patch.hpp"
#include "player_type_contract.hpp"

namespace mgscoop::player_type {
namespace {
namespace c = player_type_contract;
static_assert(c::site_offset + sizeof(c::original_bytes) <= c::body_size);
static_assert(c::body_size <= detail::max_body_size);
static_assert((c::body_rva + c::site_offset) % 2 == 0, "interlocked word exchange needs alignment");
// The image has no base relocations and loads 64-byte aligned, so the RVA
// decides cache-line placement: the whole six-byte instruction stays in one line.
static_assert((c::body_rva + c::site_offset) % 64 + sizeof(c::original_bytes) <= 64, "branch crosses a cache line");
static_assert(c::body_rva + c::site_offset + 6 + 0x79c == c::keep_path_rva, "jne/jmp target");

constexpr detail::Contract kContract{
    c::body_rva, c::body_size, c::site_offset,
    {c::original_bytes[0], c::original_bytes[1], c::original_bytes[2],
     c::original_bytes[3], c::original_bytes[4], c::original_bytes[5]},
    {c::replacement_bytes[0], c::replacement_bytes[1], c::replacement_bytes[2],
     c::replacement_bytes[3], c::replacement_bytes[4], c::replacement_bytes[5]},
    {c::body_sha256[0], c::body_sha256[1], c::body_sha256[2], c::body_sha256[3],
     c::body_sha256[4], c::body_sha256[5], c::body_sha256[6], c::body_sha256[7],
     c::body_sha256[8], c::body_sha256[9], c::body_sha256[10], c::body_sha256[11],
     c::body_sha256[12], c::body_sha256[13], c::body_sha256[14], c::body_sha256[15],
     c::body_sha256[16], c::body_sha256[17], c::body_sha256[18], c::body_sha256[19],
     c::body_sha256[20], c::body_sha256[21], c::body_sha256[22], c::body_sha256[23],
     c::body_sha256[24], c::body_sha256[25], c::body_sha256[26], c::body_sha256[27],
     c::body_sha256[28], c::body_sha256[29], c::body_sha256[30], c::body_sha256[31]}};

SRWLOCK lock = SRWLOCK_INIT;
detail::State production{nullptr, false};
unsigned char snapshot[detail::max_body_size];  // guarded by `lock`

bool Digest(const unsigned char* bytes, std::size_t size, const unsigned char* expected) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(size), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok && std::memcmp(digest, expected, sizeof(digest)) == 0;
}

bool ValidContract(const detail::Contract& contract) noexcept {
    return contract.body_size >= sizeof(contract.original) && contract.body_size <= detail::max_body_size &&
           contract.site_offset <= contract.body_size - sizeof(contract.original) &&
           std::memcmp(contract.original + 2, contract.replacement + 2, 4) == 0 &&
           std::memcmp(contract.original, contract.replacement, 2) != 0;
}

short Word(const unsigned char* bytes) noexcept {
    short value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

void LogState(void (*log)(const char*) noexcept, bool enabled, std::uintptr_t rva) noexcept {
    char line[192]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), enabled
            ? "player_type_patch=active; branch_rva=0x%llx changed_bytes=2 ocelot_quiet_snake_reset=bypassed disk_writes=0\r\n"
            : "player_type_patch=inactive; branch_rva=0x%llx original_bytes_restored=1\r\n",
            static_cast<unsigned long long>(rva)))) log(line);
}
} // namespace

namespace detail {
bool SetActiveWith(State& state, const Contract& contract, void* base, std::size_t span, bool enabled,
                   void (*log)(const char*) noexcept) noexcept {
    if (!base || !log) return false;
    const DWORD incoming = GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {
        DWORD error;
        ~Finally() { ReleaseSRWLockExclusive(&lock); SetLastError(error); }
    } finally{incoming};
    if (!ValidContract(contract)) {
        log("player_type_patch=refused; reason=invalid_contract\r\n"); return false;
    }
    // Nothing owned: disabling is a no-op and never touches foreign bytes.
    if (!enabled && !state.active) return true;
    if (state.owner && state.owner != base) {
        log("player_type_patch=refused; reason=different_image_base\r\n"); return false;
    }
    if (contract.body_rva > span || contract.body_size > span - contract.body_rva) {
        log("player_type_patch=refused; reason=span\r\n"); return false;
    }
    auto* body = static_cast<unsigned char*>(base) + contract.body_rva;
    auto* site = body + contract.site_offset;
    if (reinterpret_cast<std::uintptr_t>(site) % alignof(short)) {
        log("player_type_patch=refused; reason=branch_alignment\r\n"); return false;
    }
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), body, snapshot, contract.body_size, &read) ||
        read != contract.body_size) {
        log("player_type_patch=refused; reason=body_unreadable\r\n"); return false;
    }
    const unsigned char* expected_now = state.active ? contract.replacement : contract.original;
    if (std::memcmp(snapshot + contract.site_offset, expected_now, sizeof(contract.original)) != 0) {
        log("player_type_patch=refused; reason=branch_bytes_changed_or_unowned\r\n"); return false;
    }
    std::memcpy(snapshot + contract.site_offset, contract.original, sizeof(contract.original));
    if (!Digest(snapshot, contract.body_size, contract.sha256)) {
        log("player_type_patch=refused; reason=body_hash\r\n"); return false;
    }
    if (enabled == state.active) return true;
    const short before = Word(state.active ? contract.replacement : contract.original);
    const short after = Word(enabled ? contract.replacement : contract.original);
    auto* word = reinterpret_cast<volatile short*>(site);
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(site, sizeof(short), PAGE_EXECUTE_READWRITE, &protection)) {
        log("player_type_patch=failed; reason=page_protection\r\n"); return false;
    }
    const bool changed = _InterlockedCompareExchange16(word, after, before) == before;
    const bool flushed = changed && FlushInstructionCache(GetCurrentProcess(), site, sizeof(short)) != 0;
    if (changed && !flushed) {
        // Never leave an unflushed transition behind: return to the prior bytes.
        _InterlockedCompareExchange16(word, before, after);
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(short));
    }
    const bool restored = VirtualProtect(site, sizeof(short), protection, &ignored) != 0;
    if (changed && flushed) {
        state.active = enabled;
        state.owner = base;
    }
    if (!changed || !flushed || !restored) {
        log("player_type_patch=failed; reason=exchange_flush_or_protection_restore\r\n"); return false;
    }
    LogState(log, enabled, contract.body_rva + contract.site_offset);
    return true;
}
} // namespace detail

bool SetActive(void* base, std::size_t span, bool enabled, void (*log)(const char*) noexcept) noexcept {
    return detail::SetActiveWith(production, kContract, base, span, enabled, log);
}

bool IsActive() noexcept {
    const DWORD incoming = GetLastError();
    AcquireSRWLockShared(&lock);
    const bool active = production.active;
    ReleaseSRWLockShared(&lock);
    SetLastError(incoming);
    return active;
}
} // namespace mgscoop::player_type
