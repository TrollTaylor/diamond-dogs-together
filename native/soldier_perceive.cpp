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
#include "soldier_perceive_contract.hpp"
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

// ---------------------------------------------------------------------------------------------------------------------
// M7 step 1: per-soldier perception (pure parts).
int PickSiteIndex(std::uint32_t ret) noexcept {
    for (unsigned i = 0; i < kPickSiteCount; ++i)
        if (kPickSites[i].ret == ret) return static_cast<int>(i);
    return -1;
}

int SoldierFor(std::uint32_t ret, const std::uint64_t* regs, int tls) noexcept {
    const int site = PickSiteIndex(ret);
    if (site >= 0 && regs && kPickSites[site].answers) {
        const std::uint64_t v = regs[static_cast<unsigned>(kPickSites[site].reg)] & 0xFFFFFFFFull;
        if (v < kPickSoldiers) return static_cast<int>(v);
    }
    return tls >= 0 && tls < static_cast<int>(kPickSoldiers) ? tls : -1;
}

unsigned Answer(std::uint32_t ret, int target, unsigned stock, int forced, Leaf leaf, bool anyOne) noexcept {
    if (stock > 1) return stock;
    if (forced == 0 || forced == 1) return static_cast<unsigned>(forced);
    if (target == 0 || target == 1) return static_cast<unsigned>(target);
    if (ret == kLeafRet && leaf == Leaf::any && anyOne) return 1;
    return stock;
}

namespace {
// A target is stored as target + 1 (0 = none).
std::atomic<std::uint8_t> g_target[kPickSoldiers]{};
std::atomic<std::uint32_t> g_targetCount[2]{};
std::atomic<std::uint8_t> g_leaf{0};
struct SiteAtoms {
    std::atomic<std::uint32_t> calls{0}, inTls{0}, eqTls{0}, big{0}, min{0xFFFFFFFFu}, max{0};
};
SiteAtoms g_site[kPickSiteCount];
std::atomic<std::uint32_t> g_pc[10]{};  // PickCounters in declaration order
enum : unsigned { kCalls, kPerSoldier, kA0, kA1, kLeaf1, kTls, kUpdates, kSwaps, kMaxSoldier, kOtherThread };
void Bump(unsigned i) noexcept { g_pc[i].fetch_add(1, std::memory_order_relaxed); }
void Max(std::atomic<std::uint32_t>& a, std::uint32_t v) noexcept {
    std::uint32_t seen = a.load(std::memory_order_relaxed);
    while (v > seen && !a.compare_exchange_weak(seen, v, std::memory_order_relaxed)) {}
}
void Min(std::atomic<std::uint32_t>& a, std::uint32_t v) noexcept {
    std::uint32_t seen = a.load(std::memory_order_relaxed);
    while (v < seen && !a.compare_exchange_weak(seen, v, std::memory_order_relaxed)) {}
}
} // namespace

void SetTarget(unsigned soldier, int target) noexcept {
    if (soldier >= kPickSoldiers) return;
    const std::uint8_t want = target == 0 || target == 1 ? static_cast<std::uint8_t>(target + 1) : 0;
    const std::uint8_t old = g_target[soldier].exchange(want, std::memory_order_acq_rel);
    if (old == want) return;
    if (old) g_targetCount[old - 1].fetch_sub(1, std::memory_order_acq_rel);
    if (want) g_targetCount[want - 1].fetch_add(1, std::memory_order_acq_rel);
}

int Target(unsigned soldier) noexcept {
    if (soldier >= kPickSoldiers) return -1;
    return static_cast<int>(g_target[soldier].load(std::memory_order_acquire)) - 1;
}

void ClearTargets() noexcept {
    for (unsigned i = 0; i < kPickSoldiers; ++i) SetTarget(i, -1);
}

unsigned TargetCount(int target) noexcept {
    return target == 0 || target == 1 ? g_targetCount[target].load(std::memory_order_acquire) : 0u;
}

void SetLeaf(Leaf leaf) noexcept { g_leaf.store(static_cast<std::uint8_t>(leaf), std::memory_order_release); }
Leaf LeafMode() noexcept { return g_leaf.load(std::memory_order_acquire) == 1 ? Leaf::any : Leaf::stock; }

void CensusSite(unsigned site, std::uint64_t value, int tls) noexcept {
    if (site >= kPickSiteCount) return;
    SiteAtoms& s = g_site[site];
    s.calls.fetch_add(1, std::memory_order_relaxed);
    const std::uint32_t v = value > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(value);
    if ((value & 0xFFFFFFFFull) >= kPickSoldiers) s.big.fetch_add(1, std::memory_order_relaxed);
    Min(s.min, v);
    Max(s.max, v);
    if (tls >= 0) {
        s.inTls.fetch_add(1, std::memory_order_relaxed);
        if ((value & 0xFFFFFFFFull) == static_cast<std::uint64_t>(tls)) s.eqTls.fetch_add(1, std::memory_order_relaxed);
    }
}

SiteCensus CensusSiteRead(unsigned site) noexcept {
    SiteCensus c{};
    if (site >= kPickSiteCount) return c;
    const SiteAtoms& s = g_site[site];
    c.calls = s.calls.load(std::memory_order_relaxed);
    c.inTls = s.inTls.load(std::memory_order_relaxed);
    c.eqTls = s.eqTls.load(std::memory_order_relaxed);
    c.big = s.big.load(std::memory_order_relaxed);
    c.min = s.min.load(std::memory_order_relaxed);
    c.max = s.max.load(std::memory_order_relaxed);
    return c;
}

void CountCall(bool perSoldier, int target, bool leaf1, bool tls) noexcept {
    Bump(kCalls);
    if (perSoldier) Bump(kPerSoldier);
    if (target == 0) Bump(kA0);
    if (target == 1) Bump(kA1);
    if (leaf1) Bump(kLeaf1);
    if (tls) Bump(kTls);
}

void CountUpdate(unsigned soldier, bool swapped, bool otherThread) noexcept {
    Bump(kUpdates);
    if (swapped) Bump(kSwaps);
    if (otherThread) Bump(kOtherThread);
    Max(g_pc[kMaxSoldier], soldier);
}

PickCounters PickRead() noexcept {
    PickCounters p{};
    p.calls = g_pc[kCalls].load(std::memory_order_relaxed);
    p.perSoldier = g_pc[kPerSoldier].load(std::memory_order_relaxed);
    p.answered0 = g_pc[kA0].load(std::memory_order_relaxed);
    p.answered1 = g_pc[kA1].load(std::memory_order_relaxed);
    p.leaf1 = g_pc[kLeaf1].load(std::memory_order_relaxed);
    p.tlsCalls = g_pc[kTls].load(std::memory_order_relaxed);
    p.updates = g_pc[kUpdates].load(std::memory_order_relaxed);
    p.swaps = g_pc[kSwaps].load(std::memory_order_relaxed);
    p.maxSoldier = g_pc[kMaxSoldier].load(std::memory_order_relaxed);
    p.otherThread = g_pc[kOtherThread].load(std::memory_order_relaxed);
    return p;
}

void PickReset() noexcept {
    for (auto& a : g_pc) a.store(0, std::memory_order_relaxed);
    for (auto& s : g_site) {
        s.calls.store(0, std::memory_order_relaxed); s.inTls.store(0, std::memory_order_relaxed);
        s.eqTls.store(0, std::memory_order_relaxed); s.big.store(0, std::memory_order_relaxed);
        s.min.store(0xFFFFFFFFu, std::memory_order_relaxed); s.max.store(0, std::memory_order_relaxed);
    }
}

namespace { std::atomic<int> g_forced{-1}; }

bool PickLine(char* out, std::size_t capacity, bool installed) noexcept {
    if (!out || !capacity) return false;
    const PickCounters p = PickRead();
    const int n = std::snprintf(out, capacity,
                                "pick;on=%u;forced=%d;leaf=%s;t0=%u;t1=%u;calls=%u;soldier=%u;a0=%u;a1=%u;leaf1=%u;tls=%u;upd=%u;swaps=%u;max=%u;thr=%u",
                                installed ? 1u : 0u, g_forced.load(std::memory_order_relaxed), LeafMode() == Leaf::any ? "any" : "stock",
                                TargetCount(0), TargetCount(1), p.calls, p.perSoldier, p.answered0, p.answered1, p.leaf1, p.tlsCalls,
                                p.updates, p.swaps, p.maxSoldier, p.otherThread);
    return n >= 0 && static_cast<std::size_t>(n) < capacity;
}

void BuildStub(unsigned char (&s)[kStubSize], std::uintptr_t choose) noexcept {
    static constexpr unsigned char kCode[kStubSize] = {
        0x53,                                      // 00 push rbx
        0x55,                                      // 01 push rbp
        0x56,                                      // 02 push rsi
        0x57,                                      // 03 push rdi
        0x41, 0x54,                                // 04 push r12
        0x41, 0x55,                                // 06 push r13
        0x41, 0x56,                                // 08 push r14
        0x41, 0x57,                                // 0A push r15
        0x48, 0x83, 0xEC, 0x28,                    // 0C sub rsp, 0x28 (rsp 16-aligned at the call)
        0x48, 0x8D, 0x54, 0x24, 0x28,              // 10 lea rdx, [rsp+0x28]   (regs: r15 .. rbx)
        0x4C, 0x8B, 0x44, 0x24, 0x68,              // 15 mov r8, [rsp+0x68]    (the caller's return address)
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,        // 1A mov rax, Choose
        0xFF, 0xD0,                                // 24 call rax              (rcx = self, unchanged)
        0x48, 0x83, 0xC4, 0x28,                    // 26 add rsp, 0x28
        0x41, 0x5F,                                // 2A pop r15
        0x41, 0x5E,                                // 2C pop r14
        0x41, 0x5D,                                // 2E pop r13
        0x41, 0x5C,                                // 30 pop r12
        0x5F,                                      // 32 pop rdi
        0x5E,                                      // 33 pop rsi
        0x5D,                                      // 34 pop rbp
        0x5B,                                      // 35 pop rbx
        0xC3,                                      // 36 ret                   (eax = Choose's answer)
    };
    std::memcpy(s, kCode, kStubSize);
    std::memcpy(s + 0x1C, &choose, sizeof(choose));
}

void BuildUnwind(unsigned char (&u)[kUnwindSize]) noexcept {
    static constexpr unsigned char kInfo[kUnwindSize] = {
        0x01, 0x10, 0x09, 0x00,  // version 1, no flags; prolog 0x10 bytes; 9 codes; no frame register
        0x10, 0x42,              // 0x10: UWOP_ALLOC_SMALL 0x28
        0x0C, 0xF0,              // 0x0C: UWOP_PUSH_NONVOL r15
        0x0A, 0xE0,              // 0x0A: r14
        0x08, 0xD0,              // 0x08: r13
        0x06, 0xC0,              // 0x06: r12
        0x04, 0x70,              // 0x04: rdi
        0x03, 0x60,              // 0x03: rsi
        0x02, 0x50,              // 0x02: rbp
        0x01, 0x30,              // 0x01: rbx
        0x00, 0x00,              // padding to an even count
    };
    std::memcpy(u, kInfo, kUnwindSize);
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
    const unsigned int stock = g_offenseOriginal(self);
    const int forced = g_forced.load(std::memory_order_relaxed);
    return forced == 0 || forced == 1 ? static_cast<unsigned int>(forced) : stock;
}
} // namespace

void SetForcedIndex(int index) noexcept { g_forced.store(index == 0 || index == 1 ? index : -1, std::memory_order_relaxed); }
int ForcedIndex() noexcept { return g_forced.load(std::memory_order_relaxed); }
bool SitesInstalled() noexcept { return g_sitesOn.load(); }

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

// ---------------------------------------------------------------------------------------------------------------------
// M7 step 1: per-soldier perception (the hooks).
namespace {
namespace pc = mgscoop::soldier_perceive_contract;
thread_local int t_soldier = -1;
std::atomic<bool> g_pickOn{false};
std::atomic<DWORD> g_updateTid{0};
void* g_stubBlock = nullptr;  // stub + unwind + RUNTIME_FUNCTION; never freed once registered
using Update = void(__fastcall*)(void* sys, unsigned soldier, float dt, unsigned flag);
Update g_updateOriginal = nullptr;
inline constexpr std::size_t kStubUnwindAt = 0x38, kStubFunctionAt = 0x50, kStubBlockSize = 0x60;
static_assert(kStubSize <= kStubUnwindAt && kStubUnwindAt + kUnwindSize <= kStubFunctionAt &&
                  kStubFunctionAt + sizeof(RUNTIME_FUNCTION) <= kStubBlockSize, "stub block layout");

// Called by the stub on the caller's thread: regs = the caller's callee-saved registers (Reg order).
unsigned int __fastcall Choose(void* self, const std::uint64_t* regs, std::uintptr_t ret) {
    const std::uint32_t rva = ret > g_imageBase && ret - g_imageBase < 0x7fffffffu ? static_cast<std::uint32_t>(ret - g_imageBase) : 0;
    if (rva) SitesAdd(rva);
    const unsigned stock = g_offenseOriginal(self);
    const int tls = t_soldier;
    const int site = PickSiteIndex(rva);
    if (site >= 0) CensusSite(static_cast<unsigned>(site), regs[static_cast<unsigned>(kPickSites[site].reg)], tls);
    const int soldier = SoldierFor(rva, regs, tls);
    const int target = soldier >= 0 ? Target(static_cast<unsigned>(soldier)) : -1;
    const Leaf leaf = LeafMode();
    const bool anyOne = leaf == Leaf::any && TargetCount(1) > 0;
    const int forced = g_forced.load(std::memory_order_relaxed);
    const unsigned answer = Answer(rva, target, stock, forced, leaf, anyOne);
    CountCall(soldier >= 0, stock <= 1 && forced < 0 ? target : -1, rva == kLeafRet && answer == 1 && stock != 1, tls >= 0);
    return answer;
}

void __fastcall UpdateDetour(void* sys, unsigned soldier, float dt, unsigned flag) {
    const int prev = t_soldier;
    t_soldier = soldier < kPickSoldiers ? static_cast<int>(soldier) : -1;
    const DWORD tid = GetCurrentThreadId();
    DWORD first = 0;
    const bool other = !g_updateTid.compare_exchange_strong(first, tid, std::memory_order_relaxed) && first != tid;
    const int target = g_forced.load(std::memory_order_relaxed) < 0 && t_soldier >= 0 ? Target(soldier) : -1;
    auto* word = reinterpret_cast<volatile std::uint16_t*>(static_cast<unsigned char*>(sys) + kCacheWord);
    std::uint16_t saved = 0;
    bool swapped = false;
    if (target >= 0 && sys) {
        saved = *word;
        if (saved <= 1 && saved != static_cast<std::uint16_t>(target)) { *word = static_cast<std::uint16_t>(target); swapped = true; }
    }
    CountUpdate(soldier, swapped, other);
    g_updateOriginal(sys, soldier, dt, flag);
    if (swapped && *word == static_cast<std::uint16_t>(target)) *word = saved;
    t_soldier = prev;
}

bool WindowsMatch(const void* base, const char** which) noexcept {
    for (const pc::Window& w : pc::windows) {
        unsigned char live[64]{};
        SIZE_T read = 0;
        if (w.size > sizeof(live) ||
            !ReadProcessMemory(GetCurrentProcess(), static_cast<const unsigned char*>(base) + w.rva, live, w.size, &read) ||
            read != w.size || std::memcmp(live, w.bytes, w.size) != 0) {
            if (which) *which = w.name;
            return false;
        }
    }
    return true;
}
} // namespace

std::uintptr_t ChooseAddress() noexcept { return reinterpret_cast<std::uintptr_t>(&Choose); }
bool PickInstalled() noexcept { return g_pickOn.load(); }

bool InstallPick(const void* base, LogFn log) noexcept {
    if (!base) return false;
    if (g_pickOn.load()) return true;
    char line[160]{};
    if (g_sitesOn.load()) { Emit(log, "soldier_perceive=refused; reason=sites_hook\r\n"); return false; }
    const char* which = nullptr;
    if (!WindowsMatch(base, &which)) {
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "soldier_perceive=refused; reason=bytes; window=%s\r\n", which ? which : "?")))
            Emit(log, line);
        return false;
    }
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        Emit(log, "soldier_perceive=refused; reason=hook_initialize\r\n");
        return false;
    }
    g_imageBase = reinterpret_cast<std::uintptr_t>(base);
    if (!g_stubBlock) {
        void* block = VirtualAlloc(nullptr, kStubBlockSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!block) { Emit(log, "soldier_perceive=refused; reason=allocate\r\n"); return false; }
        auto* b = static_cast<unsigned char*>(block);
        unsigned char stub[kStubSize]{};
        BuildStub(stub, ChooseAddress());
        std::memcpy(b, stub, sizeof(stub));
        unsigned char unwind[kUnwindSize]{};
        BuildUnwind(unwind);
        std::memcpy(b + kStubUnwindAt, unwind, sizeof(unwind));
        RUNTIME_FUNCTION fn{};
        fn.BeginAddress = 0;
        fn.EndAddress = static_cast<DWORD>(kStubSize);
        fn.UnwindData = static_cast<DWORD>(kStubUnwindAt);
        std::memcpy(b + kStubFunctionAt, &fn, sizeof(fn));
        DWORD old = 0;
        if (!VirtualProtect(block, kStubBlockSize, PAGE_EXECUTE_READ, &old)) {
            VirtualFree(block, 0, MEM_RELEASE);
            Emit(log, "soldier_perceive=refused; reason=protect\r\n");
            return false;
        }
        if (!RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(b + kStubFunctionAt), 1,
                                 static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(block)))) {
            VirtualFree(block, 0, MEM_RELEASE);
            Emit(log, "soldier_perceive=refused; reason=unwind\r\n");
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), block, kStubBlockSize);
        g_stubBlock = block;
    }
    void* getter = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + kOffenseRva;
    void* update = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + kUpdateRva;
    if (!g_offenseOriginal && MH_CreateHook(getter, g_stubBlock, reinterpret_cast<void**>(&g_offenseOriginal)) != MH_OK) {
        Emit(log, "soldier_perceive=refused; reason=hook_create_getter\r\n");
        return false;
    }
    if (!g_updateOriginal && MH_CreateHook(update, reinterpret_cast<void*>(&UpdateDetour), reinterpret_cast<void**>(&g_updateOriginal)) != MH_OK) {
        Emit(log, "soldier_perceive=refused; reason=hook_create_update\r\n");
        return false;
    }
    // The update first: a thread soldier without the getter's answer changes nothing but the cache swap (targets are
    // all none until Lua or perceive_pick sets one).
    if (MH_EnableHook(update) != MH_OK) { Emit(log, "soldier_perceive=refused; reason=hook_enable_update\r\n"); return false; }
    if (MH_EnableHook(getter) != MH_OK) {
        MH_DisableHook(update);
        Emit(log, "soldier_perceive=refused; reason=hook_enable_getter\r\n");
        return false;
    }
    g_pickOn.store(true);
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                                   "soldier_perceive=installed; mode=per_soldier; getter=0x1e02770; update=0xd65850; sites=%u; windows=%u\r\n",
                                   kPickSiteCount, static_cast<unsigned>(sizeof(pc::windows) / sizeof(pc::windows[0])))))
        Emit(log, line);
    return true;
}

void LogCensus(LogFn log) noexcept {
    static const char* const kRegName[] = {"r15", "r14", "r13", "r12", "rdi", "rsi", "rbp", "rbx"};
    for (unsigned i = 0; i < kPickSiteCount; ++i) {
        const SiteCensus c = CensusSiteRead(i);
        char line[200]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                                       "soldier_perceive_census; ret=0x%x; reg=%s; answers=%u; n=%u; min=%u; max=%u; big=%u; in_tls=%u; eq_tls=%u\r\n",
                                       kPickSites[i].ret, kRegName[static_cast<unsigned>(kPickSites[i].reg)],
                                       kPickSites[i].answers ? 1u : 0u, c.calls, c.calls ? c.min : 0u, c.max, c.big, c.inTls, c.eqTls)))
            Emit(log, line);
    }
}
#endif

} // namespace mgscoop::soldier_perceive
