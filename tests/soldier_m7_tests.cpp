// Enemy sync M7 (docs/world-sync-design.md 7.13): the perception patch block check and the soldier damage authority.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/soldier_authority.hpp"
#include "mgscoop/soldier_perceive.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
unsigned checks = 0;
void Check(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
#ifdef _WIN32
// The executed stub test: what the stub hands to Choose, and a stack walk from inside it.
void* g_self = nullptr;
const std::uint64_t* g_regs = nullptr;
std::uintptr_t g_ret = 0, g_slot = 0;
USHORT g_frames = 0;
void* g_trace[8]{};
DWORD64 g_unwound[2]{};
unsigned int __fastcall TestChoose(void* self, const std::uint64_t* regs, std::uintptr_t ret) {
    g_self = self;
    g_regs = regs;
    g_ret = ret;
    g_slot = static_cast<std::uintptr_t>(regs[8]);  // the slot above rbx is the caller's return address
    g_frames = RtlCaptureStackBackTrace(0, 8, g_trace, nullptr);
    // Unwind by hand with the registered data: this frame, then the stub's.
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    for (unsigned i = 0; i < 2; ++i) {
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fe) break;
        PVOID handler = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fe, &ctx, &handler, &establisher, nullptr);
        g_unwound[i] = ctx.Rip;
    }
    return 0x1234;
}
char g_logged[256]{};
void CaptureLog(const char* line) noexcept {
    std::size_t n = std::strlen(line);
    if (n >= sizeof(g_logged)) n = sizeof(g_logged) - 1;
    std::memcpy(g_logged, line, n);
    g_logged[n] = 0;
}
#endif

// P13: the LOCAL player accessors' redirect (soldier_perceive.hpp kLocalRecRva / kLocalIdxRva).
#ifdef _WIN32
unsigned char g_records[3][0xE0]{};  // [0] the fake local record, [1] / [2] the fake records 0 and 1
unsigned g_recIdx = 99, g_localRecCalls = 0, g_localIdxCalls = 0;
void* __fastcall FakeLocalRec() { ++g_localRecCalls; return g_records[0]; }
bool __fastcall FakeLocalIdx(std::uint32_t* out) { ++g_localIdxCalls; *out = 0; return true; }
void* __fastcall FakeRec(unsigned idx) { g_recIdx = idx; return idx < 2 ? g_records[1 + idx] : nullptr; }
#endif
void TestLocal() {
    namespace sp = mgscoop::soldier_perceive;
    using K = sp::LocalKind;
    // Who asked: the thunk's caller sits 0x30 above a LOCAL record read's entry; slot 0x208's caller above its index read.
    Check(sp::LocalCaller(K::rec, 0x1E02E0E, 0xD59430) == 0xD59430, "a thunk's record read: the thunk's caller");
    Check(sp::LocalCaller(K::rec, 0x1E03202, 0x14DCA77) == 0x14DCA77 && sp::LocalCaller(K::rec, 0x1E02000, 7) == 7,
          "every return inside the thunk range");
    Check(sp::LocalCaller(K::rec, 0x1E04000, 0xD59430) == 0x1E04000 && sp::LocalCaller(K::rec, 0x1D00000, 0xD59430) == 0x1D00000,
          "a direct caller outside the thunks is itself");
    Check(sp::LocalCaller(K::idx, sp::kIdThunkRet, 0x1350100) == 0x1350100, "slot 0x208's index read: its caller");
    Check(sp::LocalCaller(K::idx, 0x134E8AE, 0x1D00000) == 0x134E8AE && sp::LocalCaller(K::idx, 0x1E02B50, 9) == 0x1E02B50,
          "slot 0x1f8's tail jump: the return is the caller's own");
    Check(sp::LocalScoped(0xD59430) && sp::LocalScoped(0x14DCA77) && sp::LocalScoped(0xD50000) && sp::LocalScoped(0x15FFFFF),
          "the notice and soldier code are scoped");
    Check(!sp::LocalScoped(0xD4FFFF) && !sp::LocalScoped(0xD80000) && !sp::LocalScoped(0x1600000) && !sp::LocalScoped(0x1E02E0E) &&
              !sp::LocalScoped(0),
          "the player, camera, UI and the thunks themselves are not");
    Check(sp::LocalMode() == -1, "the redirect is off by default");
    sp::SetLocalMode(1);
    Check(sp::LocalMode() == 1, "mode 1");
    sp::SetLocalMode(2);
    Check(sp::LocalMode() == sp::kLocalPerSoldier, "per soldier");
    sp::SetLocalMode(3);
    Check(sp::LocalMode() == sp::kLocalAll, "every caller (research)");
    sp::SetLocalMode(4);
    Check(sp::LocalMode() == -1, "any other value turns it off");
    sp::SetLocalMode(-7);
    Check(sp::LocalMode() == -1, "negative: off");
    Check(sp::LocalAnswer(sp::kLocalAll, false, -1) == 1 && sp::LocalAnswer(sp::kLocalAll, true, 0) == 1,
          "every caller, scoped or not, reads body 1");
    Check(sp::LocalAnswer(-1, true, 1) == -1 && sp::LocalAnswer(1, false, 1) == -1 && sp::LocalAnswer(0, false, 1) == -1,
          "off or unscoped: stock");
    Check(sp::LocalAnswer(1, true, -1) == 1 && sp::LocalAnswer(1, true, 0) == 1 && sp::LocalAnswer(0, true, 1) == 0,
          "mode 0/1: every scoped call reads that player");
    Check(sp::LocalAnswer(2, true, 1) == 1 && sp::LocalAnswer(2, true, 0) == 0 && sp::LocalAnswer(2, true, -1) == -1 &&
              sp::LocalAnswer(2, true, 5) == -1,
          "per soldier: the soldier's target, else stock");
    // The census: per (caller, entry return), calls, redirects and the register ranges.
    sp::LocalReset();
    Check(!sp::LocalCensusOn(), "the census is off by default");
    std::uint64_t regs[16]{};
    regs[6] = 0x1'0000'0005ull;  // rbp: only the low 32 bits count
    regs[3] = 200;               // r12
    sp::LocalCount(0xD59430, 0x1E02E0E, true, regs);
    regs[6] = 9;
    regs[3] = 180;
    sp::LocalCount(0xD59430, 0x1E02E0E, false, regs);
    sp::LocalCount(0xD59430, 0x1E02E0E, false, nullptr);
    sp::LocalCount(0x1350100, sp::kIdThunkRet, true, regs);
    sp::LocalEntry e[4]{};
    std::uint32_t overflow = 7;
    Check(sp::LocalRead(e, 4, &overflow) == 2 && overflow == 0, "two entries");
    Check(e[0].caller == 0xD59430 && e[0].entry == 0x1E02E0E && e[0].calls == 3 && e[0].redirected == 1, "most calls first");
    Check(e[0].min[0] == 5 && e[0].max[0] == 9 && e[0].min[3] == 180 && e[0].max[3] == 200,
          "rbp 5..9 and r12 180..200 (entry index rbp=0, rsi, rdi, r12=3, r13, r14, r15)");
    Check(e[1].caller == 0x1350100 && e[1].entry == sp::kIdThunkRet && e[1].calls == 1 && e[1].redirected == 1, "the index read");
    Check(sp::LocalRead(e, 1, nullptr) == 1 && e[0].calls == 3, "a short buffer keeps the top");
    for (std::uint32_t i = 0; i < sp::kLocalCensus + 5; ++i) sp::LocalCount(0x1400000 + i * 16, 0x1E02E0E, false, nullptr);
    Check(sp::LocalRead(e, 1, &overflow) == 1 && overflow == 7, "256 entries, the rest counted as overflow (2 + 254 fit)");
    sp::LocalTally(K::rec, true, true);
    sp::LocalTally(K::rec, false, false);
    sp::LocalTally(K::idx, true, false);
    const sp::LocalCounters lc = sp::LocalCountersRead();
    Check(lc.rec == 2 && lc.idx == 1 && lc.scoped == 2 && lc.redirected == 1, "the tallies");
    char line[160]{};
    sp::SetLocalMode(1);
    sp::SetLocalCensus(true);
    Check(sp::LocalLine(line, sizeof(line)) &&
              std::strcmp(line, "local;mode=1;census=1;rec=2;idx=1;scoped=2;redirected=1;online=-1;online_writes=0") == 0,
          "the local line");
    Check(!sp::LocalLine(line, 16), "a short buffer refuses");
    sp::LocalReset();
    sp::SetLocalCensus(false);
    Check(sp::LocalRead(e, 4, &overflow) == 0 && overflow == 0 && sp::LocalCountersRead().rec == 0, "reset");
    // L4: the online bit and its inverse in the manager's mode dword.
    Check(sp::OnlineBits(0x2, true) == 0x4 && sp::OnlineBits(0x2 | 0x8 | 0x40, true) == (0x4 | 0x8 | 0x40) &&
              sp::OnlineBits(0x4 | 0x10, false) == (0x2 | 0x10) && sp::OnlineBits(0x6, true) == 0x4,
          "online sets bit 2 and clears bit 1; off the reverse; other bits kept");
    Check(sp::OnlineMode() == -1, "online untouched by default");
    sp::SetOnline(1);
    Check(sp::OnlineMode() == 1, "online on");
    sp::SetOnline(0);
    Check(sp::OnlineMode() == 0, "online off (restore once)");
    sp::SetOnline(4);
    Check(sp::OnlineMode() == -1 && sp::OnlineWrites() == 0, "any other value: untouched; nothing written in the test");
#ifdef _WIN32
    // The stubs' C++ halves with fake game functions: the image base 0x140000000, a fabricated register block (regs[8]
    // the entry return, regs[14] the slot 0x30 above it).
    using Choose = std::uint64_t(__fastcall*)(void*, const std::uint64_t*, std::uintptr_t);
    const auto rec = reinterpret_cast<Choose>(sp::LocalRecChooseAddress());
    const auto idx = reinterpret_cast<Choose>(sp::LocalIdxChooseAddress());
    sp::SetLocalTargets(0x140000000, &FakeLocalRec, &FakeLocalIdx, &FakeRec);
    std::uint64_t r[16]{};
    r[8] = 0x140000000 + 0x1E02E0E;   // slot 0x18's thunk
    r[14] = 0x140000000 + 0xD59430;   // asked by the notice code
    sp::SetLocalMode(-1);
    Check(rec(nullptr, r, r[8]) == reinterpret_cast<std::uint64_t>(g_records[0]) && g_localRecCalls == 1, "off: the stock record");
    sp::SetLocalMode(1);
    Check(rec(nullptr, r, r[8]) == reinterpret_cast<std::uint64_t>(g_records[2]) && g_recIdx == 1 && g_localRecCalls == 1,
          "mode 1: record 1 for a scoped caller, the stock function not called");
    r[14] = 0x140000000 + 0x1D00000;  // asked by code outside the soldier ranges
    Check(rec(nullptr, r, r[8]) == reinterpret_cast<std::uint64_t>(g_records[0]) && g_localRecCalls == 2, "unscoped: stock");
    r[8] = 0x140000000 + 0x1350100;   // a direct caller in the soldier code (no thunk)
    Check(rec(nullptr, r, r[8]) == reinterpret_cast<std::uint64_t>(g_records[2]), "a scoped direct caller is redirected");
    std::uint32_t out = 77;
    r[8] = 0x140000000 + sp::kIdThunkRet;
    r[14] = 0x140000000 + 0x134EF2E;  // slot 0x208 asked by the soldier manager
    Check(idx(&out, r, r[8]) == 1 && out == 1 && g_localIdxCalls == 0, "mode 1: the local index reads 1");
    sp::SetLocalMode(0);
    out = 77;
    Check(idx(&out, r, r[8]) == 1 && out == 0 && g_localIdxCalls == 0, "mode 0: index 0 (the answer, not the stock call)");
    sp::SetLocalMode(-1);
    Check(idx(&out, r, r[8]) == 1 && g_localIdxCalls == 1, "off: the stock index");
    const sp::LocalCounters after = sp::LocalCountersRead();
    Check(after.rec == 4 && after.idx == 3 && after.scoped == 6 && after.redirected == 4, "the stubs tally every call");
    sp::LocalReset();
    sp::SetLocalTargets(0, nullptr, nullptr, nullptr);
#endif
}
}  // namespace

int main() {
    namespace sp = mgscoop::soldier_perceive;
    namespace sa = mgscoop::soldier_authority;
    // The block: the patched dword sits at offset 0x3b (74 65 8b 55), one cache line, and only it may differ.
    Check(sp::kPatchOffset == 0x3b && sp::kBlockRva + sp::kPatchOffset == sp::kPatchRva, "the patch dword is at 0x132562b");
    Check(sp::kBlock[0x3b] == 0x74 && sp::kBlock[0x3c] == 0x65 && sp::kBlock[0x3d] == 0x8b && sp::kBlock[0x3e] == 0x55,
          "the original dword is je +0x65 and the next instruction's head");
    Check(((sp::kPatchRva & 63u) + 4u) <= 64u, "the dword does not span two cache lines");
    unsigned char live[sp::kBlockSize];
    std::memcpy(live, sp::kBlock, sizeof(live));
    bool patched = true;
    Check(sp::BlockMatches(live, sizeof(live), &patched) && !patched, "the retail block matches, unpatched");
    live[0x3b] = 0xeb;
    Check(sp::BlockMatches(live, sizeof(live), &patched) && patched, "the patched block matches, patched");
    live[0x3c] = 0x66;
    Check(!sp::BlockMatches(live, sizeof(live), &patched), "another branch distance refuses");
    std::memcpy(live, sp::kBlock, sizeof(live));
    live[0x20] ^= 1;
    Check(!sp::BlockMatches(live, sizeof(live)), "any other changed byte refuses");
    Check(!sp::BlockMatches(nullptr, sizeof(live)) && !sp::BlockMatches(sp::kBlock, sizeof(live) - 1), "null or short refuses");
    // The authority: a soldier's hit (GameObjectId type 2) on a player.
    constexpr std::uint16_t soldier = (2u << 9) | 37u, player = 0, dog = (5u << 9) | 1u;
    Check(sa::IsSoldier(soldier) && !sa::IsSoldier(player) && !sa::IsSoldier(dog) && !sa::IsSoldier(0xFFFF), "soldier ids");
    Check(sa::Decide(false, 1, 0, soldier, false) == sa::Action::stock, "off: stock");
    Check(sa::Decide(true, 0, 0, player, false) == sa::Action::stock, "a player attacker: stock");
    Check(sa::Decide(true, 0, 0, soldier, false) == sa::Action::stock, "an owned guard on this player: stock");
    Check(sa::Decide(true, 1, 0, soldier, false) == sa::Action::forward, "an owned guard on the replica: forward");
    Check(sa::Decide(true, 0, 0, soldier, true) == sa::Action::neutralize, "a remote guard on this player: neutralize");
    Check(sa::Decide(true, 1, 0, soldier, true) == sa::Action::neutralize, "a remote guard on the replica: neutralize");
    Check(sa::Decide(true, 2, 0, soldier, false) == sa::Action::stock && sa::Decide(true, 0, 2, soldier, false) == sa::Action::stock,
          "only players 0 and 1");
    Check(std::strcmp(sa::ActionName(sa::Action::forward), "forward") == 0, "action names");
    // Ownership bits and the role default.
    sa::Configure(true, false);
    sa::Clear();
    Check(sa::Enabled() && !sa::Remote(soldier) && sa::RemoteCount() == 0, "host: unknown soldiers are its own");
    Check(sa::Set(37, true) && sa::Remote(soldier) && sa::RemoteCount() == 1, "host: a guest-owned guard is remote");
    Check(!sa::Remote(player) && !sa::Remote(dog), "only soldiers are ever remote");
    Check(sa::Set(37, false) && !sa::Remote(soldier), "back to the host");
    Check(!sa::Set(512, true), "index 0..511");
    sa::Configure(true, true);
    sa::Clear();
    Check(sa::Remote(soldier) && sa::RemoteCount() == sa::kSoldiers, "guest: unknown soldiers are the host's");
    Check(sa::Set(37, false) && !sa::Remote(soldier) && sa::RemoteCount() == sa::kSoldiers - 1, "guest: its own guard");
    sa::Clear();
    Check(sa::Remote(soldier), "clear: the role default again");
    sa::Configure(false, false);
    Check(!sa::Enabled(), "off");
    // Probe P11 census: the scorer head pin and the category-5 word counts.
    Check(sp::kScorerHead[0] == 0x40 && sp::kScorerHead[1] == 0x55 && sp::kScorerHead[0x3b] == 0xb8, "the scorer head pin");
    sp::CensusReset();
    const unsigned char one[] = {0x10, 0x00, 0x00, 0x05, 0x22, 0x04, 0x00, 0x03};  // a player word 0x10, a category-3 entry
    sp::CensusAdd(one, 2);
    sp::CensusAdd(nullptr, 0);
    const unsigned char two[] = {0x10, 0x00, 0x00, 0x05, 0x11, 0x00, 0x00, 0x05};
    sp::CensusAdd(two, 2);
    auto cs = sp::CensusRead();
    Check(cs.calls == 3 && cs.entries == 4 && cs.players == 3 && cs.multi == 1 && cs.maxPerCall == 2 && cs.overflow == 0,
          "census counters");
    Check(cs.words == 2 && cs.word[0] == 0x10 && cs.count[0] == 2 && cs.word[1] == 0x11 && cs.count[1] == 1, "census words");
    char line[161]{};
    Check(sp::CensusLine(line, sizeof(line)) && std::strstr(line, "players=3;multi=1;max=2;overflow=0;words=10:2,11:1") != nullptr,
          "census line");
    for (unsigned w = 0; w < sp::kCensusWords + 2; ++w) {
        const unsigned char e[] = {static_cast<unsigned char>(0x40 + w), 0x00, 0x00, 0x05};
        sp::CensusAdd(e, 1);
    }
    cs = sp::CensusRead();
    Check(cs.words == sp::kCensusWords && cs.overflow == 4, "census keeps 8 words and counts the rest as overflow");
    Check(!sp::CensusLine(line, 16), "a short buffer refuses");
    sp::CensusReset();
    Check(sp::CensusRead().calls == 0 && sp::CensusRead().words == 0, "census reset");
    // Probe P11 sites: the getter head pin and the return address counts.
    Check(sp::kOffenseHead[0] == 0x48 && sp::kOffenseHead[4] == 0xe8 && sp::kOffenseHead[0x1f] == 0x50, "the getter head pin");
    sp::SitesReset();
    sp::SitesAdd(0x132561f);
    sp::SitesAdd(0x132561f);
    sp::SitesAdd(0x1d00000);
    auto st = sp::SitesRead();
    Check(st.sites == 2 && st.rva[0] == 0x132561f && st.count[0] == 2 && st.rva[1] == 0x1d00000 && st.count[1] == 1, "sites");
    Check(sp::SitesLine(line, sizeof(line)) && std::strcmp(line, "sites;n=2;overflow=0;132561f:2,1d00000:1") == 0, "sites line");
    for (unsigned i = 0; i < sp::kSites + 1; ++i) sp::SitesAdd(0x100 + i);
    st = sp::SitesRead();
    Check(st.sites == sp::kSites && st.overflow == 3, "sites keep 24 and count the rest");
    Check(sp::SitesLine(line, sizeof(line)) && std::strlen(line) < sizeof(line), "a long list is cut to fit");
    Check(!sp::SitesLine(line, 8), "a short buffer refuses");
    sp::SitesReset();
    Check(sp::SitesRead().sites == 0, "sites reset");
    // Probe P11b: the forced index is 0, 1 or off (-1).
    Check(sp::ForcedIndex() == -1, "not forced by default");
    sp::SetForcedIndex(1);
    Check(sp::ForcedIndex() == 1, "forced 1");
    sp::SetForcedIndex(2);
    Check(sp::ForcedIndex() == -1, "another index turns it off");
    sp::SetForcedIndex(0);
    Check(sp::ForcedIndex() == 0, "forced 0");
    sp::SetForcedIndex(-1);
    Check(sp::ForcedIndex() == -1 && !sp::SitesInstalled(), "off; the hook is not installed in the test");
    // ---- M7 step 1: per-soldier perception ---------------------------------------------------------------------
    Check(sp::kPickSiteCount == 8 && sp::PickSiteIndex(0xD6B87F) == 0 && sp::PickSiteIndex(0xD56834) == 6 &&
              sp::PickSiteIndex(0x14DCA77) == 7 && sp::PickSiteIndex(0xD5AF51) == -1 && sp::PickSiteIndex(sp::kLeafRet) == -1,
          "the pick sites");
    // Saved registers in the stub's order: r15, r14, r13, r12, rdi, rsi, rbp, rbx.
    std::uint64_t regs[9] = {15, 14, 13, 12, 7, 6, 5, 3, 0};
    Check(sp::SoldierFor(0xD59430, regs, -1) == 12, "the answering site reads its own register (r12)");
    Check(sp::SoldierFor(0xD6B87F, regs, -1) == -1 && sp::SoldierFor(0xD6D9CE, regs, -1) == -1 && sp::SoldierFor(0xD6638E, regs, -1) == -1 &&
              sp::SoldierFor(0xD782E3, regs, -1) == -1 && sp::SoldierFor(0xD56834, regs, -1) == -1 && sp::SoldierFor(0x14DCA77, regs, -1) == -1,
          "run 96216259: the notice-system sites are census only (their register is one slot, 45, never a guard)");
    Check(sp::SoldierFor(0x14DCA77, regs, 9) == 9, "a census-only site falls back to the thread soldier");
    regs[3] = 0xFFFFFFFF00000020ull;
    Check(sp::SoldierFor(0xD59430, regs, -1) == 0x20, "only the low 32 bits (mov r12d, edx)");
    regs[3] = 600;
    Check(sp::SoldierFor(0xD59430, regs, 11) == 11 && sp::SoldierFor(0xD59430, regs, -1) == -1,
          "an out-of-range register falls back to the thread soldier");
    Check(sp::SoldierFor(0xD5AF51, regs, 4) == 4 && sp::SoldierFor(0xD5AF51, regs, -1) == -1 &&
              sp::SoldierFor(0xD5AF51, regs, 512) == -1, "other callers: the thread soldier only");
    using L = sp::Leaf;
    Check(sp::Answer(0xD6B87F, -1, 0, -1, L::stock, false) == 0 && sp::Answer(0xD6B87F, 1, 0, -1, L::stock, false) == 1 &&
              sp::Answer(0xD6B87F, 0, 1, -1, L::stock, false) == 0, "the target replaces the stock answer");
    Check(sp::Answer(0xD6B87F, 0, 0, 1, L::stock, false) == 1 && sp::Answer(0xD6B87F, 1, 0, 0, L::stock, false) == 0,
          "a forced index wins");
    Check(sp::Answer(0xD6B87F, 1, 0xFFFF, -1, L::stock, false) == 0xFFFF && sp::Answer(0xD6B87F, 1, 2, 1, L::stock, false) == 2,
          "a stock answer other than 0 or 1 is never replaced");
    Check(sp::Answer(sp::kLeafRet, -1, 0, -1, L::any, true) == 1 && sp::Answer(sp::kLeafRet, -1, 0, -1, L::any, false) == 0 &&
              sp::Answer(sp::kLeafRet, -1, 0, -1, L::stock, true) == 0 && sp::Answer(0xD5AF51, -1, 0, -1, L::any, true) == 0,
          "the leaf snapshot: 1 in mode any while a soldier targets 1, only at its own call");
    sp::ClearTargets();
    Check(sp::Target(37) == -1 && sp::TargetCount(0) == 0 && sp::TargetCount(1) == 0, "no targets");
    sp::SetTarget(37, 1);
    sp::SetTarget(38, 0);
    sp::SetTarget(39, 1);
    sp::SetTarget(39, 1);
    Check(sp::Target(37) == 1 && sp::Target(38) == 0 && sp::TargetCount(1) == 2 && sp::TargetCount(0) == 1, "targets counted once");
    sp::SetTarget(37, 0);
    sp::SetTarget(39, 5);
    sp::SetTarget(600, 1);
    Check(sp::Target(37) == 0 && sp::Target(39) == -1 && sp::TargetCount(1) == 0 && sp::TargetCount(0) == 2 && sp::Target(600) == -1,
          "a change moves the count; another value clears; out of range ignored");
    sp::ClearTargets();
    Check(sp::TargetCount(0) == 0 && sp::TargetCount(1) == 0, "cleared");
    sp::SetLeaf(L::any);
    Check(sp::LeafMode() == L::any, "leaf any");
    sp::SetLeaf(L::stock);
    // The P12a census and the counters.
    sp::PickReset();
    sp::CensusSite(0, 7, -1);
    sp::CensusSite(0, 9, 9);
    sp::CensusSite(0, 700, 3);
    sp::CensusSite(sp::kPickSiteCount, 1, -1);
    const auto c0 = sp::CensusSiteRead(0);
    Check(c0.calls == 3 && c0.inTls == 2 && c0.eqTls == 1 && c0.big == 1 && c0.min == 7 && c0.max == 700, "the site census");
    sp::CountCall(true, 1, false, true);
    sp::CountCall(false, -1, true, false);
    sp::CountUpdate(40, true, false);
    sp::CountUpdate(12, false, true);
    const auto pk = sp::PickRead();
    Check(pk.calls == 2 && pk.perSoldier == 1 && pk.answered1 == 1 && pk.answered0 == 0 && pk.leaf1 == 1 && pk.tlsCalls == 1 &&
              pk.updates == 2 && pk.swaps == 1 && pk.maxSoldier == 40 && pk.otherThread == 1, "the pick counters");
    Check(sp::PickLine(line, sizeof(line), true) &&
              std::strcmp(line, "pick;on=1;forced=-1;leaf=stock;t0=0;t1=0;calls=2;soldier=1;a0=0;a1=1;leaf1=1;tls=1;upd=2;swaps=1;max=40;thr=1") == 0,
          "the pick line");
    Check(!sp::PickLine(line, 20, true), "a short buffer refuses");
    sp::PickReset();
    Check(sp::PickRead().calls == 0 && sp::CensusSiteRead(0).calls == 0 && sp::CensusSiteRead(0).min == 0xFFFFFFFFu, "reset");
    // The stub and its unwind data.
    unsigned char stub[sp::kStubSize]{};
    sp::BuildStub(stub, 0x1122334455667788ull);
    std::uint64_t imm = 0;
    std::memcpy(&imm, stub + 0x1C, sizeof(imm));
    Check(stub[0] == 0x53 && stub[0x0B] == 0x57 && stub[0x0C] == 0x48 && stub[0x0F] == 0x28 && stub[0x1A] == 0x48 &&
              stub[0x1B] == 0xB8 && imm == 0x1122334455667788ull && stub[0x24] == 0xFF && stub[0x25] == 0xD0 &&
              stub[sp::kStubSize - 1] == 0xC3, "the stub bytes and the Choose address");
    unsigned char unwind[sp::kUnwindSize]{};
    sp::BuildUnwind(unwind);
    Check(unwind[0] == 1 && unwind[1] == 0x10 && unwind[2] == 9 && unwind[4] == 0x10 && unwind[5] == 0x42 && unwind[6] == 0x0C &&
              unwind[7] == 0xF0 && unwind[20] == 0x01 && unwind[21] == 0x30, "the unwind codes: alloc 0x28 and eight pushes");
#ifdef _WIN32
    {
        // Run the stub for real: Choose gets self, the saved registers and the caller's return address, its answer comes
        // back, and a stack walk from inside Choose unwinds through the stub into this function.
        constexpr std::size_t kUnwindAt = 0x38, kFunctionAt = 0x50, kBlock = 0x60;
        auto* block = static_cast<unsigned char*>(VirtualAlloc(nullptr, kBlock, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        Check(block != nullptr, "allocate the stub block");
        sp::BuildStub(stub, reinterpret_cast<std::uintptr_t>(&TestChoose));
        std::memcpy(block, stub, sizeof(stub));
        std::memcpy(block + kUnwindAt, unwind, sizeof(unwind));
        RUNTIME_FUNCTION fn{};
        fn.BeginAddress = 0;
        fn.EndAddress = static_cast<DWORD>(sp::kStubSize);
        fn.UnwindData = static_cast<DWORD>(kUnwindAt);
        std::memcpy(block + kFunctionAt, &fn, sizeof(fn));
        DWORD old = 0;
        Check(VirtualProtect(block, kBlock, PAGE_EXECUTE_READ, &old) != 0, "protect the stub block");
        Check(RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(block + kFunctionAt), 1,
                                  static_cast<DWORD64>(reinterpret_cast<std::uintptr_t>(block))) != 0, "register the unwind data");
        FlushInstructionCache(GetCurrentProcess(), block, kBlock);
        using Getter = unsigned int(__fastcall*)(void*);
        const auto getter = reinterpret_cast<Getter>(block);
        int self = 0;
        const unsigned answer = getter(&self);
        Check(answer == 0x1234 && g_self == &self, "the stub passes self and returns Choose's answer");
        Check(g_ret != 0 && g_slot == g_ret, "ret is the caller's return address, right above the saved registers");
        const auto stubStart = reinterpret_cast<std::uintptr_t>(block);
        if (GetEnvironmentVariableA("SOLDIER_M7_TRACE", nullptr, 0) != 0) {
            std::printf("stub=%llx ret=%llx frames=%u\n", static_cast<unsigned long long>(stubStart), static_cast<unsigned long long>(g_ret), g_frames);
            for (unsigned i = 0; i < g_frames; ++i) std::printf("  %u %p\n", i, g_trace[i]);
        }
        if (GetEnvironmentVariableA("SOLDIER_M7_TRACE", nullptr, 0) != 0)
            { std::printf("unwound %llx %llx", static_cast<unsigned long long>(g_unwound[0]), static_cast<unsigned long long>(g_unwound[1])); std::puts(""); }
        Check(g_frames >= 2 && reinterpret_cast<std::uintptr_t>(g_trace[1]) > stubStart &&
                  reinterpret_cast<std::uintptr_t>(g_trace[1]) < stubStart + sp::kStubSize,
              "a stack walk from inside Choose finds the stub");
        Check(g_unwound[0] == stubStart + 0x26 && g_unwound[1] == g_ret,
              "the stub's unwind data leads back to its caller's return address");
        // Callee-saved registers survive: call it in a loop with live values across the calls.
        unsigned sum = 0;
        for (unsigned i = 0; i < 64; ++i) sum += getter(&self) == 0x1234 ? i : 1000;
        Check(sum == 64 * 63 / 2, "repeated calls keep this frame intact");
        // P12e: without a per-slot update seen there is no notice system to read.
        sp::LogSlots(&CaptureLog);
        Check(std::strstr(g_logged, "soldier_perceive_slot; none=1") == g_logged, "no slots before the update hook ran");
        RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(block + kFunctionAt));
        VirtualFree(block, 0, MEM_RELEASE);
    }
#endif
    // P12d: the player info record of an index (0x14053CA40).
    Check(sp::BodyRecord(0x140000000, 0, 0) == 0x140000000 + 0x2BD3FF0 && sp::BodyRecord(0x140000000, 0, 1) == 0x140000000 + 0x2BD3FF0 + 0xE0 &&
              sp::BodyRecord(0x140000000, 1, 1) == 0x140000000 + 0x2BD3FF0 + 17 * 0xE0,
          "the record of an index: (sel * 16 + idx) * 0xE0 past the bank");
    Check(sp::BodyRecord(0x140000000, 0, 16) == 0 && sp::BodyRecord(0x140000000, -1, 0) == 0 && sp::BodyRecord(0x140000000, 16, 0) == 0 &&
              sp::BodyRecord(0, 0, 0) == 0, "out of range: none");
    // P12d research masks: offsets 0xC0..0xCB, two slots, mask 0 = off.
    unsigned mo = 0, mm = 0;
    Check(sp::SetBodyMask(0, 0xCB, 0x01) && sp::SetBodyMask(1, 0xC4, 0x20), "two masks");
    sp::BodyMask(0, mo, mm);
    Check(mo == 0xCB && mm == 0x01, "slot 0: +0xCB bit 0");
    sp::BodyMask(1, mo, mm);
    Check(mo == 0xC4 && mm == 0x20, "slot 1: +0xC4 bit 5");
    Check(!sp::SetBodyMask(2, 0xC0, 1) && !sp::SetBodyMask(0, 0xBF, 1) && !sp::SetBodyMask(0, 0xE0, 1) && !sp::SetBodyMask(0, 0xC0, 0x100),
          "slot and offset bounds");
    // P13 L3 (run 29e389d7): body 1 alone carries status 138 (+0xD1 bit 2); the mask reaches the whole bitset.
    Check(sp::SetBodyMask(0, 0xD1, 0x04), "+0xD1 (status 138) is in range");
    sp::BodyMask(0, mo, mm);
    Check(mo == 0xD1 && mm == 0x04, "slot 0: +0xD1 bit 2");
    Check(sp::SetBodyMask(1, 0xDF, 0x80) && sp::SetBodyMask(0, 0xC0, 0) && sp::SetBodyMask(1, 0xC0, 0), "+0xDF is the last byte");
    Check(!sp::BodyMaskSets(0) && sp::SetBodyMask(0, 0xC0, 0x88, true) && sp::BodyMaskSets(0), "a set mask");
    sp::BodyMask(0, mo, mm);
    Check(mo == 0xC0 && mm == 0x88, "slot 0: set +0xC0 bits 0x88");
    Check(sp::SetBodyMask(0, 0xC0, 0) && sp::SetBodyMask(1, 0xC0, 0) && !sp::BodyMaskSets(0), "off");
    sp::BodyMask(0, mo, mm);
    Check(mm == 0 && sp::BodyMaskWrites() == 0, "off, nothing written in the test");
    // The soldiers' hits on each player as the defense callback sees them.
    sa::DefenseReset();
    sa::NoteDefense(1, soldier);
    sa::NoteDefense(1, soldier);
    sa::NoteDefense(1, static_cast<std::uint16_t>((2u << 9) | 3u));
    sa::NoteDefense(0, static_cast<std::uint16_t>((2u << 9) | 300u));
    sa::NoteDefense(1, player);
    sa::NoteDefense(1, dog);
    sa::NoteDefense(2, soldier);
    const auto def = sa::DefenseRead();
    Check(def.hits[1] == 3 && def.hits[0] == 1, "soldier hits per target only");
    char dl[64]{};
    Check(sa::DefenseList(def, 1, dl, sizeof(dl)) && std::strcmp(dl, "3:3,37") == 0, "body 1: 3 hits by soldiers 3 and 37");
    Check(sa::DefenseList(def, 0, dl, sizeof(dl)) && std::strcmp(dl, "1:300") == 0, "body 0: 1 hit by soldier 300");
    Check(!sa::DefenseList(def, 2, dl, sizeof(dl)) && !sa::DefenseList(def, 1, dl, 1), "target 0 or 1; a buffer for the head");
    sa::DefenseReset();
    Check(sa::DefenseRead().hits[1] == 0 && sa::DefenseRead().by[1][0] == 0, "defense reset");
    TestLocal();
    std::printf("soldier_m7_tests: %u checks passed\n", checks);
    return 0;
}
