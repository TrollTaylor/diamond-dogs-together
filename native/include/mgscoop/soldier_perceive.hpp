#pragma once
// Enemy sync M7 (docs/world-sync-design.md 7.13): the soldier perception filter patch. In the per-soldier reaction chain
// (0x1413262F0 calls 0x141325530 for perceived category-5 entries, the players), a player-type target (GameObjectId type
// 0) is skipped unless its index equals the FOB offense player index (vtable 0x1425564F0 slot 0x220, 0x141E02770: the
// local player offline). So no soldier ever reacts to body 1, the partner. The patch turns the `je 0x141325692` at
// 0x14132562B (74 65) into `jmp` (EB 65): every player-type entry goes on to the stock checks (0x141325692: the per-target
// ignore test, then the distance/priority scoring 0x141325E90). Offline only players 0 and 1 exist.
// Static evidence: mgs-coop-re/m7 (Ghidra 12.0 project mgsvtpp-local, mgsvtpp.exe 1.0.15.4 SHA-256 085c2f82...bb45):
// the block below, the vtable slot, the 18 offense-index call sites (only this one filters perception).
#include <cstddef>
#include <cstdint>

namespace mgscoop::soldier_perceive {

inline constexpr std::uintptr_t kBlockRva = 0x13255F0;  // mov rax,[rcx] .. jne 0x1413255c0 (0x50 bytes)
inline constexpr std::size_t kBlockSize = 0x50;
inline constexpr std::uintptr_t kPatchRva = 0x132562B;  // the dword 74 65 8b 55 (je +0x65; mov edx,[rbp-0x44] head)
inline constexpr std::size_t kPatchOffset = kPatchRva - kBlockRva;
inline constexpr std::uint32_t kOld = 0x558b6574u, kNew = 0x558b65ebu;
// The block as in the retail executable (the patched dword included as 74 65 8b 55).
extern const unsigned char kBlock[kBlockSize];

// Pure: the live block equals kBlock, the patched dword (kPatchOffset .. +4) either original or patched. *patched (when
// given) is true for the patched form.
bool BlockMatches(const unsigned char* live, std::size_t size, bool* patched = nullptr) noexcept;

#ifdef _WIN32
using LogFn = void (*)(const char* line) noexcept;
// `base` is the image base. Patch() verifies the block and exchanges the dword (near_code::PatchDword), logging
// soldier_perceive=patched; Revert() restores it (soldier_perceive=reverted; reason=...). A mismatch or a failed exchange
// logs soldier_perceive=refused and changes nothing. Idempotent.
bool Patch(const void* base, LogFn log) noexcept;
void Revert(const void* base, LogFn log, const char* reason) noexcept;
bool Patched() noexcept;
#endif

// Probe P11 census (with the patch): which category-5 entries a soldier's player scorer 0x141325530 is given. The scorer
// (called directly from the reaction chain 0x1413262F0 at 0x14132638E: rcx the chain, edx the soldier index) reads
// count = ((u32*)L[1])[edx] and entries = ((u8**)L[2])[edx] with L = *(*(rcx+0x58)+0x88) (L[1] at +8, L[2] at +0x10); an
// entry is 4 bytes: the u16 perception word (its low 9 bits are converted to the GameObjectId) and the category at +3.
// The census reads exactly those fields before the stock body runs (it never calls a game function) and counts the
// distinct category-5 words: one word while both players stand near the guards means body 1 is not in the soldier's list.
inline constexpr std::uintptr_t kScorerRva = 0x1325530;
inline constexpr std::size_t kScorerHeadSize = 0x40;
extern const unsigned char kScorerHead[kScorerHeadSize];
inline constexpr unsigned kCensusWords = 8;
struct CensusSnapshot {
    std::uint32_t calls = 0, entries = 0, players = 0, multi = 0, maxPerCall = 0, overflow = 0;
    std::uint16_t word[kCensusWords]{};
    std::uint32_t count[kCensusWords]{};
    unsigned words = 0;
};
// Pure: one scorer call's entries (4 bytes each). Lock-free; any thread.
void CensusAdd(const unsigned char* entries, std::uint32_t count) noexcept;
CensusSnapshot CensusRead() noexcept;
void CensusReset() noexcept;
// "perceive;census=<0|1>;calls=;entries=;players=;multi=;max=;overflow=;words=<hex>:<n>,..."
bool CensusLine(char* out, std::size_t capacity) noexcept;
#ifdef _WIN32
// Verifies kScorerHead at kScorerRva and hooks the scorer (MinHook) to feed the census. Logs
// soldier_perceive_census=installed | refused; reason=.... Idempotent.
bool InstallCensus(const void* base, LogFn log) noexcept;
#endif

// Probe P11 sites (run af17b899: the scorer above was never called in free roam while soldiers shot both players): which
// callers of the offense player index getter 0x141E02770 (GetFobOffensePlayerInstanceIndex; 18 static call sites) run.
// The getter is hooked only to count its return addresses (rva) and returns the stock value.
inline constexpr std::uintptr_t kOffenseRva = 0x1E02770;
inline constexpr std::size_t kOffenseHeadSize = 0x20;
extern const unsigned char kOffenseHead[kOffenseHeadSize];
inline constexpr unsigned kSites = 24;
struct SitesSnapshot {
    std::uint32_t rva[kSites]{};
    std::uint32_t count[kSites]{};
    unsigned sites = 0;
    std::uint32_t overflow = 0;
};
void SitesAdd(std::uint32_t rva) noexcept;  // pure, lock-free
SitesSnapshot SitesRead() noexcept;
void SitesReset() noexcept;
// "sites;n=<sites>;overflow=;<rva hex>:<n>,..." (as many as fit); false when even the head does not fit.
bool SitesLine(char* out, std::size_t capacity) noexcept;
#ifdef _WIN32
// Verifies kOffenseHead and hooks the getter (MinHook; refuses when another hook already owns it, e.g. player_team).
bool InstallSites(const void* base, LogFn log) noexcept;
#endif

} // namespace mgscoop::soldier_perceive
