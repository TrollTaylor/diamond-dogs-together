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
// Probe P11b: while forced (0 or 1; -1 = off, the stock value), the hooked getter returns that index to every caller
// (Dynamite's single offense player, here fixed by the debug command perceive_force 0|1|off).
void SetForcedIndex(int index) noexcept;
int ForcedIndex() noexcept;
bool SitesInstalled() noexcept;
#endif

// ---------------------------------------------------------------------------------------------------------------------
// M7 step 1: per-soldier perception (lab key soldier_perceive, off unless named; docs/world-sync-design.md 7.13). The
// scorer patch above is no longer installed (run af17b899: the scorer never runs in free roam). The single-player soldier
// AI reads ONE "offense" player from the getter everywhere (run 32a93d27). Here the getter answers per soldier; which
// soldier asks comes from
//   - the caller's soldier register at the live per-soldier call sites (each function keeps its edx soldier index in a
//     callee-saved register up to the getter call; pinned in soldier_perceive_contract.hpp), read by a stub that saves
//     rbx, rbp, rsi, rdi, r12..r15 before it calls Choose, or
//   - the notice system's per-slot update 0x140D65850(sys, edx slot, xmm2 dt, r9b flag) (MinHook): a thread-local
//     current slot for the P12a census only (run 96216259: the slots are not guards, so the cached index word sys+0x1ac,
//     written once a frame at 0x140D5AF51 and read at 0x140D65908, is no longer swapped).
// A soldier's target (0 = the local player, 1 = body 1, the partner) is set by Lua (the host's nearer-player rule,
// MGS_COOP_SOLDIER_PERCEIVE_V1) or by the debug command perceive_pick; a soldier without one gets the stock answer. The
// once-a-frame leaf snapshot (0x14134F9D0 at ret 0x1350035: [leaf+0x28c] index, +0x1d0 position, +0x294 state bits; one
// for all soldiers of the leaf) keeps the stock answer unless the leaf mode is `any` (1 while any soldier targets 1).
inline constexpr unsigned kPickSoldiers = 512;
// Index into the stub's saved registers (ascending addresses: the last push first).
enum class Reg : std::uint8_t { r15 = 0, r14, r13, r12, rdi, rsi, rbp, rbx, count };
struct PickSite {
    std::uint32_t ret;  // return address rva of the getter call
    Reg reg;            // the caller's soldier register
    bool answers;       // false: census only
};
// Run 96216259 (P12a census, both copies): at the notice-system sites the register is the same slot, 45, on every call
// (the per-soldier update 0x140D65850 runs slots 0..76, and only slot 45 reaches these calls; the thread soldier agrees),
// so their index is not a guard's ([I]: the local player's own slot in the notice system); they are census only. Only
// 0xD59430's r12 (176..272, the range of the guards' GameObjectId indices) answers.
inline constexpr PickSite kPickSites[] = {
    {0xD6B87F, Reg::rsi, false},  // 0x140D6B7E0 the notice gauge (mov esi,edx at 0x140D6B81C)
    {0xD6D82F, Reg::rsi, false},  // 0x140D6D7B0 distance (0x140D6D7ED)
    {0xD6D9CE, Reg::r14, false},  // 0x140D6D930 state bit skip (0x140D6D950)
    {0xD6638E, Reg::r15, false},  // 0x140D661F0 masks (0x140D6622E), called from the per-soldier update
    {0xD782E3, Reg::rbx, false},  // 0x140D78250 the 6-player table per slot (0x140D78260)
    {0xD59430, Reg::r12, true},   // 0x140D59320 soldier records (0x140D59340)
    {0xD56834, Reg::rbp, false},  // 0x140D567F0 slot 0x88 position (movzx ebp,dx at 0x140D5681F)
    {0x14DCA77, Reg::rdi, false}, // 0x1414DC9D0 (0x1414DC9FC), guest only
};
inline constexpr unsigned kPickSiteCount = sizeof(kPickSites) / sizeof(kPickSites[0]);
inline constexpr std::uint32_t kLeafRet = 0x1350035;
inline constexpr std::uintptr_t kUpdateRva = 0xD65850;
inline constexpr std::size_t kCacheWord = 0x1ac;
enum class Leaf : std::uint8_t { stock = 0, any = 1 };

// Pure. The table index of a return rva, or -1.
int PickSiteIndex(std::uint32_t ret) noexcept;
// Pure. The soldier a getter call asks for: the site's register (when the site answers and the register holds an index
// below kPickSoldiers), else the thread's current soldier (tls >= 0), else -1.
int SoldierFor(std::uint32_t ret, const std::uint64_t* regs, int tls) noexcept;
// Pure. The answer: a forced index (0 or 1) to every caller; else the soldier's target; else, at the leaf snapshot in
// mode any, 1 while any soldier targets 1; else stock. Only a stock answer of 0 or 1 is ever replaced.
unsigned Answer(std::uint32_t ret, int target, unsigned stock, int forced, Leaf leaf, bool anyOne) noexcept;

// Targets (lock-free; the Lua owner thread writes, the AI threads read). -1 = none (stock).
void SetTarget(unsigned soldier, int target) noexcept;  // target 0 or 1; any other value clears
int Target(unsigned soldier) noexcept;
void ClearTargets() noexcept;
unsigned TargetCount(int target) noexcept;  // soldiers with that target (0 or 1)
void SetLeaf(Leaf leaf) noexcept;
Leaf LeafMode() noexcept;

// P12a census: per pick site, the captured register values, and how often a call with a thread soldier agrees with it.
struct SiteCensus {
    std::uint32_t calls = 0, inTls = 0, eqTls = 0, big = 0, min = 0xFFFFFFFFu, max = 0;
};
void CensusSite(unsigned site, std::uint64_t value, int tls) noexcept;
SiteCensus CensusSiteRead(unsigned site) noexcept;
struct PickCounters {
    std::uint32_t calls = 0, perSoldier = 0, answered0 = 0, answered1 = 0, leaf1 = 0, tlsCalls = 0;
    std::uint32_t updates = 0, swaps = 0, maxSoldier = 0, otherThread = 0;
};
void CountCall(bool perSoldier, int target, bool leaf1, bool tls) noexcept;
void CountUpdate(unsigned soldier, bool swapped, bool otherThread) noexcept;
PickCounters PickRead() noexcept;
void PickReset() noexcept;  // counters and census (not the targets)
// "pick;on=;forced=;leaf=;t0=;t1=;calls=;soldier=;a0=;a1=;leaf1=;tls=;upd=;swaps=;max=;thr="
bool PickLine(char* out, std::size_t capacity, bool installed) noexcept;

// The stub (any address; MinHook jumps to it): pushes the eight callee-saved registers, then calls
// Choose(self, regs, ret) with regs = the saved block (Reg order) and ret = the caller's return address.
inline constexpr std::size_t kStubSize = 0x37;
inline constexpr std::size_t kUnwindSize = 24;
void BuildStub(unsigned char (&out)[kStubSize], std::uintptr_t choose) noexcept;
void BuildUnwind(unsigned char (&out)[kUnwindSize]) noexcept;

// P12d: the player info records the AI reads through the service's slot 0 (0x141E02D70 -> 0x14053CA40): for an index
// below 16, rec = image + kBankRva + (sel * 16 + idx) * kBankStride with sel = the int32 at image + kBankSelRva;
// position at +0x10, state bytes at +0xC0..+0xCB (the AI skips a player with bit 5 of +0xC4 at 0x140D6D9E6 and one with
// bit 0 of +0xCB at 0x141390CA6). Pure; 0 when idx >= 16 or sel is outside 0..15.
inline constexpr std::uintptr_t kBankSelRva = 0x2BD3FD0, kBankRva = 0x2BD3FF0;
inline constexpr std::size_t kBankStride = 0xE0;
std::uintptr_t BodyRecord(std::uintptr_t image, std::int32_t sel, unsigned idx) noexcept;

// P12d research (debug command perceive_mask; never on unless commanded): before the leaf snapshot's getter call (ret
// kLeafRet, once a frame on the AI thread) clear (or, with `set`, set) `mask` in byte +offset (0xC0..0xCB) of body 1's
// record in both banks, up to two pairs. A pair with mask 0 is off. Run 37c9604c: body 0 / body 1 differ only in +0xC0
// (0x9e / 0x16 in one bank: bits 0x80 and 0x08).
inline constexpr unsigned kBodyMasks = 2;
bool SetBodyMask(unsigned slot, unsigned offset, unsigned mask, bool set = false) noexcept;  // false: out of range
void BodyMask(unsigned slot, unsigned& offset, unsigned& mask) noexcept;
bool BodyMaskSets(unsigned slot) noexcept;
std::uint32_t BodyMaskWrites() noexcept;

#ifdef _WIN32
// "bodies;sel=<n>;b0=<x>,<y>,<z>;s0=<12 hex bytes>;b1=...;s1=...;o0=<+0xC0..+0xC3 of body 0 in the other bank>;o1=..."
// read with ReadProcessMemory (no game call).
bool BodiesLine(const void* base, char* out, std::size_t capacity) noexcept;
// Verifies every pinned window (soldier_perceive_contract.hpp) and installs the getter stub hook and the per-soldier
// update hook. Logs soldier_perceive=installed; mode=per_soldier | refused; reason=.... Idempotent. Takes the place of
// InstallSites (the same getter; the site census and perceive_force run through Choose).
bool InstallPick(const void* base, LogFn log) noexcept;
bool PickInstalled() noexcept;
// One soldier_perceive_census line per pick site to `log`.
void LogCensus(LogFn log) noexcept;
// P12e (read only, ReadProcessMemory): the notice system's slots as the per-slot update 0x140D65850 reads them, from the
// sys pointer the update hook last saw: count u32 sys+0x198 (the loop bound, [rsi+0x170] at 0x140D5B00B); per slot the
// 16-byte record [sys+0x208]+slot*16 (+0xd bit 0 = active, 0x140D6589C), the dword [sys+0x220][slot] (0x140D658AF),
// and its index list [sys+0x150] from word [sys+0x158][slot], count word [sys+0x160][slot] (0x140D6591E..0x140D6592A;
// the entries index the 24-byte records [sys+0x78], the same records 0x140D59320 indexes by a guard's GameObjectId
// index). One soldier_perceive_slot line per active slot (at most 96 slots, 12 list entries each).
void LogSlots(LogFn log) noexcept;
// Test seam: the stub calls this C++ function (a pointer the test may read).
std::uintptr_t ChooseAddress() noexcept;
#endif

} // namespace mgscoop::soldier_perceive
