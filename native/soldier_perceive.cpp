// Original MGS Co-op: the soldier perception filter patch (docs/world-sync-design.md 7.13; header soldier_perceive.hpp).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mgscoop/soldier_perceive.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <strsafe.h>
#include <intrin.h>
#include "MinHook.h"
#include "mgscoop/near_code.hpp"
#endif

namespace mgscoop::soldier_perceive {

const unsigned char kBlock[kBlockSize] = {
    0x48, 0x8b, 0x01, 0xff, 0x50, 0x50, 0x0f, 0xb7, 0xf8, 0xe8, 0x52, 0x9a, 0x8d, 0xff, 0x48, 0x8b,
    0x88, 0x98, 0x00, 0x00, 0x00, 0xb8, 0x00, 0xfe, 0x00, 0x00, 0x48, 0x8b, 0x89, 0xa0, 0x01, 0x00,
    0x00, 0x66, 0x85, 0xf8, 0x75, 0x77, 0x48, 0x8b, 0x01, 0xff, 0x90, 0x20, 0x02, 0x00, 0x00, 0x0f,
    0xb7, 0xcf, 0xbf, 0xff, 0x01, 0x00, 0x00, 0x23, 0xcf, 0x3b, 0xc8, 0x74, 0x65, 0x8b, 0x55, 0xbc,
    0x4c, 0x8b, 0x45, 0xd0, 0x4c, 0x8b, 0xce, 0x49, 0x83, 0xc6, 0x04, 0x49, 0xff, 0xcd, 0x75, 0x80,
};

bool BlockMatches(const unsigned char* live, std::size_t size, bool* patched) noexcept {
    if (patched) *patched = false;
    if (!live || size != kBlockSize) return false;
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        if (i >= kPatchOffset && i < kPatchOffset + 4) continue;
        if (live[i] != kBlock[i]) return false;
    }
    std::uint32_t dword = 0;
    std::memcpy(&dword, live + kPatchOffset, sizeof(dword));
    if (dword != kOld && dword != kNew) return false;
    if (patched) *patched = dword == kNew;
    return true;
}

// The scorer's first 0x40 bytes (push rbp .. mov eax,0xffff), as in the retail executable.
const unsigned char kScorerHead[kScorerHeadSize] = {
    0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83,
    0xec, 0x70, 0x48, 0x8b, 0x05, 0xe7, 0x3d, 0x82, 0x01, 0x48, 0x33, 0xc4, 0x48, 0x89, 0x45, 0xf0,
    0x48, 0x8b, 0x41, 0x58, 0x8b, 0xf2, 0x49, 0x8b, 0xd9, 0x4c, 0x8b, 0xa0, 0xc8, 0x00, 0x00, 0x00,
    0x4c, 0x89, 0x45, 0xe0, 0x4c, 0x8b, 0x80, 0x88, 0x00, 0x00, 0x00, 0xb8, 0xff, 0xff, 0x00, 0x00,
};

namespace {
std::atomic<std::uint32_t> g_calls{0}, g_entries{0}, g_players{0}, g_multi{0}, g_max{0}, g_overflow{0};
// A slot holds word + 1 (0 = empty).
std::atomic<std::uint32_t> g_word[kCensusWords]{};
std::atomic<std::uint32_t> g_count[kCensusWords]{};
void AddWord(std::uint16_t word) noexcept {
    const std::uint32_t key = std::uint32_t(word) + 1u;
    for (unsigned i = 0; i < kCensusWords; ++i) {
        std::uint32_t seen = g_word[i].load(std::memory_order_relaxed);
        if (seen == 0 && g_word[i].compare_exchange_strong(seen, key, std::memory_order_relaxed)) seen = key;
        if (seen == key) { g_count[i].fetch_add(1, std::memory_order_relaxed); return; }
    }
    g_overflow.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

void CensusAdd(const unsigned char* entries, std::uint32_t count) noexcept {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (!entries || !count) return;
    g_entries.fetch_add(count, std::memory_order_relaxed);
    std::uint32_t players = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const unsigned char* e = entries + std::size_t(i) * 4u;
        if (e[3] != 5) continue;
        ++players;
        std::uint16_t word = 0;
        std::memcpy(&word, e, sizeof(word));
        AddWord(word);
    }
    if (!players) return;
    g_players.fetch_add(players, std::memory_order_relaxed);
    if (players > 1) g_multi.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t max = g_max.load(std::memory_order_relaxed);
    while (players > max && !g_max.compare_exchange_weak(max, players, std::memory_order_relaxed)) {}
}

CensusSnapshot CensusRead() noexcept {
    CensusSnapshot s{};
    s.calls = g_calls.load(std::memory_order_relaxed);
    s.entries = g_entries.load(std::memory_order_relaxed);
    s.players = g_players.load(std::memory_order_relaxed);
    s.multi = g_multi.load(std::memory_order_relaxed);
    s.maxPerCall = g_max.load(std::memory_order_relaxed);
    s.overflow = g_overflow.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < kCensusWords; ++i) {
        const std::uint32_t key = g_word[i].load(std::memory_order_relaxed);
        if (!key) continue;
        s.word[s.words] = static_cast<std::uint16_t>(key - 1u);
        s.count[s.words] = g_count[i].load(std::memory_order_relaxed);
        ++s.words;
    }
    return s;
}

void CensusReset() noexcept {
    std::atomic<std::uint32_t>* const counters[] = {&g_calls, &g_entries, &g_players, &g_multi, &g_max, &g_overflow};
    for (auto* a : counters) a->store(0, std::memory_order_relaxed);
    for (unsigned i = 0; i < kCensusWords; ++i) { g_count[i].store(0, std::memory_order_relaxed); g_word[i].store(0, std::memory_order_relaxed); }
}

namespace {
std::atomic<bool> g_censusOn{false};
}

bool CensusLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const CensusSnapshot s = CensusRead();
    int n = std::snprintf(out, capacity, "perceive;census=%u;calls=%u;entries=%u;players=%u;multi=%u;max=%u;overflow=%u;words=",
                          g_censusOn.load(std::memory_order_relaxed) ? 1u : 0u, s.calls, s.entries, s.players, s.multi, s.maxPerCall,
                          s.overflow);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) return false;
    std::size_t used = static_cast<std::size_t>(n);
    for (unsigned i = 0; i < s.words; ++i) {
        n = std::snprintf(out + used, capacity - used, "%s%x:%u", i ? "," : "", unsigned(s.word[i]), s.count[i]);
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - used) return false;
        used += static_cast<std::size_t>(n);
    }
    return true;
}


// The getter's first 0x20 bytes (sub rsp,0x28; call; mov rcx,[rax+0x78]; ...), as targeting_contract.hpp pins them.
const unsigned char kOffenseHead[kOffenseHeadSize] = {
    0x48, 0x83, 0xec, 0x28, 0xe8, 0xd7, 0xc8, 0xdf, 0xfe, 0x48, 0x8b, 0x48, 0x78, 0x48, 0x8b, 0x01,
    0xff, 0x50, 0x40, 0x48, 0x8b, 0xc8, 0x48, 0x85, 0xc0, 0x74, 0x1b, 0x48, 0x8b, 0x00, 0xff, 0x50,
};

namespace {
std::atomic<std::uint32_t> g_siteRva[kSites]{};  // rva + 1 (0 = empty)
std::atomic<std::uint32_t> g_siteCount[kSites]{};
std::atomic<std::uint32_t> g_siteOverflow{0};
}

void SitesAdd(std::uint32_t rva) noexcept {
    const std::uint32_t key = rva + 1u;
    for (unsigned i = 0; i < kSites; ++i) {
        std::uint32_t seen = g_siteRva[i].load(std::memory_order_relaxed);
        if (seen == 0 && g_siteRva[i].compare_exchange_strong(seen, key, std::memory_order_relaxed)) seen = key;
        if (seen == key) { g_siteCount[i].fetch_add(1, std::memory_order_relaxed); return; }
    }
    g_siteOverflow.fetch_add(1, std::memory_order_relaxed);
}

SitesSnapshot SitesRead() noexcept {
    SitesSnapshot s{};
    for (unsigned i = 0; i < kSites; ++i) {
        const std::uint32_t key = g_siteRva[i].load(std::memory_order_relaxed);
        if (!key) continue;
        s.rva[s.sites] = key - 1u;
        s.count[s.sites] = g_siteCount[i].load(std::memory_order_relaxed);
        ++s.sites;
    }
    s.overflow = g_siteOverflow.load(std::memory_order_relaxed);
    return s;
}

void SitesReset() noexcept {
    for (unsigned i = 0; i < kSites; ++i) { g_siteCount[i].store(0, std::memory_order_relaxed); g_siteRva[i].store(0, std::memory_order_relaxed); }
    g_siteOverflow.store(0, std::memory_order_relaxed);
}

bool SitesLine(char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const SitesSnapshot s = SitesRead();
    int n = std::snprintf(out, capacity, "sites;n=%u;overflow=%u;", s.sites, s.overflow);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) return false;
    std::size_t used = static_cast<std::size_t>(n);
    for (unsigned i = 0; i < s.sites; ++i) {
        char item[32]{};
        const int k = std::snprintf(item, sizeof(item), "%s%x:%u", i ? "," : "", s.rva[i], s.count[i]);
        if (k < 0 || static_cast<std::size_t>(k) >= capacity - used) break;
        std::memcpy(out + used, item, static_cast<std::size_t>(k) + 1u);
        used += static_cast<std::size_t>(k);
    }
    return true;
}

#ifdef _WIN32
namespace {
volatile LONG g_patched = 0;
void Emit(LogFn log, const char* line) noexcept { if (log) log(line); }
bool Live(const void* base, unsigned char (&out)[kBlockSize]) noexcept {
    SIZE_T read = 0;
    return base && ReadProcessMemory(GetCurrentProcess(), static_cast<const unsigned char*>(base) + kBlockRva, out, kBlockSize, &read) &&
           read == kBlockSize;
}
} // namespace

bool Patch(const void* base, LogFn log) noexcept {
    if (!base) return false;
    if (g_patched) return true;
    unsigned char live[kBlockSize]{};
    bool already = false;
    if (!Live(base, live) || !BlockMatches(live, kBlockSize, &already)) {
        Emit(log, "soldier_perceive=refused; reason=block_bytes\r\n");
        return false;
    }
    void* site = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + kPatchRva;
    if (!already) {
        if (const char* why = near_code::PatchDword(site, kOld, kNew)) {
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "soldier_perceive=refused; reason=patch_%s\r\n", why))) Emit(log, line);
            return false;
        }
    }
    InterlockedExchange(&g_patched, 1);
    Emit(log, "soldier_perceive=patched; rva=0x132562b; old=74; new=eb; site=player_filter\r\n");
    return true;
}

void Revert(const void* base, LogFn log, const char* reason) noexcept {
    if (!base || !g_patched) return;
    void* site = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + kPatchRva;
    if (const char* why = near_code::PatchDword(site, kNew, kOld)) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "soldier_perceive=refused; reason=revert_%s\r\n", why))) Emit(log, line);
        return;
    }
    InterlockedExchange(&g_patched, 0);
    char line[96]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "soldier_perceive=reverted; reason=%s\r\n", reason ? reason : "unknown")))
        Emit(log, line);
}

bool Patched() noexcept { return g_patched != 0; }

namespace {
using OffenseIndex = unsigned int(__fastcall*)(void* self);
OffenseIndex g_offenseOriginal = nullptr;
std::uintptr_t g_imageBase = 0;
std::atomic<bool> g_sitesOn{false};
unsigned int __fastcall SitesOffense(void* self) {
    const auto ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (ret > g_imageBase && ret - g_imageBase < 0x7fffffffu) SitesAdd(static_cast<std::uint32_t>(ret - g_imageBase));
    return g_offenseOriginal(self);
}
} // namespace

bool InstallSites(const void* base, LogFn log) noexcept {
    if (!base) return false;
    if (g_sitesOn.load()) return true;
    unsigned char live[kOffenseHeadSize]{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), static_cast<const unsigned char*>(base) + kOffenseRva, live, sizeof(live), &read) ||
        read != sizeof(live) || std::memcmp(live, kOffenseHead, sizeof(live)) != 0) {
        Emit(log, "soldier_perceive_sites=refused; reason=head_bytes\r\n");
        return false;
    }
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        Emit(log, "soldier_perceive_sites=refused; reason=hook_initialize\r\n");
        return false;
    }
    g_imageBase = reinterpret_cast<std::uintptr_t>(base);
    void* target = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + kOffenseRva;
    if (!g_offenseOriginal && MH_CreateHook(target, reinterpret_cast<void*>(&SitesOffense), reinterpret_cast<void**>(&g_offenseOriginal)) != MH_OK) {
        Emit(log, "soldier_perceive_sites=refused; reason=hook_create\r\n");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Emit(log, "soldier_perceive_sites=refused; reason=hook_enable\r\n");
        return false;
    }
    g_sitesOn.store(true);
    Emit(log, "soldier_perceive_sites=installed; rva=0x1e02770\r\n");
    return true;
}

namespace {
using Scorer = void(__fastcall*)(void* chain, unsigned soldier, void* out, void* extra);
Scorer g_original = nullptr;
// Reads exactly the fields the stock body reads next (same thread, same frame) and runs the stock body unchanged.
void __fastcall CensusScorer(void* chain, unsigned soldier, void* out, void* extra) {
    const auto* c = static_cast<const unsigned char*>(chain);
    const unsigned char* p = c ? *reinterpret_cast<const unsigned char* const*>(c + 0x58) : nullptr;
    const unsigned char* list = p ? *reinterpret_cast<const unsigned char* const*>(p + 0x88) : nullptr;
    const auto* counts = list ? *reinterpret_cast<const std::uint32_t* const*>(list + 0x8) : nullptr;
    const auto* const* tables = list ? *reinterpret_cast<const unsigned char* const* const*>(list + 0x10) : nullptr;
    if (counts && tables) {
        const std::uint32_t count = counts[soldier];
        if (count <= 256) CensusAdd(count ? tables[soldier] : nullptr, count);
    }
    g_original(chain, soldier, out, extra);
}
} // namespace

bool InstallCensus(const void* base, LogFn log) noexcept {
    if (!base) return false;
    if (g_censusOn.load()) return true;
    unsigned char live[kScorerHeadSize]{};
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), static_cast<const unsigned char*>(base) + kScorerRva, live, sizeof(live), &read) ||
        read != sizeof(live) || std::memcmp(live, kScorerHead, sizeof(live)) != 0) {
        Emit(log, "soldier_perceive_census=refused; reason=head_bytes\r\n");
        return false;
    }
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        Emit(log, "soldier_perceive_census=refused; reason=hook_initialize\r\n");
        return false;
    }
    void* target = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + kScorerRva;
    if (!g_original && MH_CreateHook(target, reinterpret_cast<void*>(&CensusScorer), reinterpret_cast<void**>(&g_original)) != MH_OK) {
        Emit(log, "soldier_perceive_census=refused; reason=hook_create\r\n");
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Emit(log, "soldier_perceive_census=refused; reason=hook_enable\r\n");
        return false;
    }
    g_censusOn.store(true);
    Emit(log, "soldier_perceive_census=installed; rva=0x1325530; caller=0x14132638e\r\n");
    return true;
}
#endif

} // namespace mgscoop::soldier_perceive
