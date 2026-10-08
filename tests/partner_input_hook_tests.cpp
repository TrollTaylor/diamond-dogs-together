// Round 3 input hook: stub bytes, rel32 reach, the dispatch-block gate and the
// locked dword patcher, on synthetic memory (no game, no real install).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_input_hook.hpp"

namespace {
namespace nc = mgscoop::near_code;
namespace hk = mgscoop::partner_input_hook;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

void TestStubBytes() {
    unsigned char stub[14]{};
    const void* target = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x1122334455667788ull));
    hk::detail::BuildStub(stub, target);
    Check(stub[0] == 0xFF && stub[1] == 0x25 && stub[2] == 0 && stub[3] == 0 && stub[4] == 0 && stub[5] == 0,
          "absolute indirect jmp FF 25 00000000");
    std::uintptr_t stored = 0;
    std::memcpy(&stored, stub + 6, 8);
    Check(stored == reinterpret_cast<std::uintptr_t>(target), "abs64 target follows");
}

void TestRel32() {
    std::int32_t rel = 0;
    Check(nc::Rel32(0x1000, 0x1005, rel) && rel == 5, "forward rel32");
    Check(nc::Rel32(0x2000, 0x1000, rel) && rel == -0x1000, "backward rel32");
    Check(!nc::Rel32(0, 0x80000000ull, rel), "out of int32 range refused");
    Check(nc::CrossesCacheLine(63, 4) && !nc::CrossesCacheLine(60, 4), "cache-line split detection");
}

void TestBlockGate() {
    unsigned char block[64]{};
    const std::size_t n = hk::detail::BlockSize();
    // A matching block (the real bytes come from the gate; here just make a
    // self-consistent block that BlockMatches accepts: opcode 0xE8 at offset 17).
    // BlockMatches compares against the fixed contract bytes, so copy them by
    // asking it: build the known-good block by starting from a mismatch and
    // confirming rejection, then the exact bytes accept.
    static const unsigned char good[] = {0x84, 0xd2, 0x75, 0x17, 0xba, 0x08, 0x00, 0x00, 0x00, 0x49, 0x8b, 0x8c, 0x00, 0x90,
                                          0x01, 0x00, 0x00, 0xe8, 0x7a, 0x36, 0x23, 0x00, 0xe9, 0xcc, 0x00, 0x00, 0x00};
    Check(n == sizeof(good), "block size matches");
    std::memcpy(block, good, n);
    Check(hk::detail::BlockMatches(block), "pristine block accepted");
    // The rel32 operand (offset 18..21) may differ (already patched).
    block[18] = 0x11; block[19] = 0x22; block[20] = 0x33; block[21] = 0x44;
    Check(hk::detail::BlockMatches(block), "retargeted rel32 still accepted");
    // A changed opcode is refused.
    std::memcpy(block, good, n);
    block[0] = 0x00;
    Check(!hk::detail::BlockMatches(block), "byte mismatch refused");
    std::memcpy(block, good, n);
    block[17] = 0xE9;  // not a call
    Check(!hk::detail::BlockMatches(block), "non-call opcode refused");
}

void TestPatchDword() {
    void* page = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    Check(page != nullptr, "page allocated");
    auto* site = static_cast<unsigned char*>(page) + 0x40;  // aligned, within one cache line
    std::uint32_t orig = 0xC7440F44u, repl = 0x00401F0Fu;
    std::memcpy(site, &orig, 4);
    Check(nc::PatchDword(site, orig, repl) == nullptr, "patch succeeds");
    std::uint32_t now = 0; std::memcpy(&now, site, 4);
    Check(now == repl, "dword replaced");
    // Idempotent / CAS refusal: expecting the old value now fails (changed).
    const char* why = nc::PatchDword(site, orig, repl);
    Check(why && std::strcmp(why, "changed") == 0, "CAS refusal when the dword is not the expected value");
    // Revert.
    Check(nc::PatchDword(site, repl, orig) == nullptr, "revert");
    std::memcpy(&now, site, 4);
    Check(now == orig, "reverted");
    // A split-line dword is refused.
    auto* split = static_cast<unsigned char*>(page) + 62;
    Check(nc::PatchDword(split, 0, 1) != nullptr, "split-line dword refused");
    VirtualFree(page, 0, MEM_RELEASE);
}

// Round 4: which injected word each PP key-manager slot answers from, and
// whether it takes a binding index or a key handle (0x1206260..0x12063D0).
void TestKmSlots() {
    using W = hk::detail::KmWord;
    Check(hk::detail::KmSlotWord(0xE8) == W::press && hk::detail::KmSlotWord(0x110) == W::press &&
          hk::detail::KmSlotWord(0x118) == W::press, "pressed slots");
    Check(hk::detail::KmSlotWord(0xF0) == W::release && hk::detail::KmSlotWord(0x120) == W::release &&
          hk::detail::KmSlotWord(0x128) == W::release, "released slots");
    Check(hk::detail::KmSlotWord(0x100) == W::held && hk::detail::KmSlotWord(0x108) == W::held &&
          hk::detail::KmSlotWord(0x130) == W::held && hk::detail::KmSlotWord(0x138) == W::held, "held slots");
    Check(hk::detail::KmSlotWord(0xF8) == W::zero && hk::detail::KmSlotWord(0x90) == W::none, "km vt+0x148 answers 0; others untouched");
    Check(hk::detail::KmSlotByIndex(0xE8) && hk::detail::KmSlotByIndex(0x100) && hk::detail::KmSlotByIndex(0x130) &&
          !hk::detail::KmSlotByIndex(0x108) && !hk::detail::KmSlotByIndex(0x118) && !hk::detail::KmSlotByIndex(0x138),
          "index slots vs handle slots");
}

// The vtable data pointer swap (wrap entries) on a read-only page.
void TestPatchQword() {
    void* page = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(page != nullptr, "data page allocated");
    auto* slot = reinterpret_cast<std::uint64_t*>(static_cast<unsigned char*>(page) + 0x28);
    *slot = 0x140BF6420ull;
    DWORD old = 0;
    Check(VirtualProtect(page, 0x1000, PAGE_READONLY, &old) != 0, "page read-only");
    Check(nc::PatchQword(slot, 0x140BF6420ull, 0x7FF600001234ull) == nullptr && *slot == 0x7FF600001234ull, "qword swapped");
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(page, &info, sizeof(info));
    Check(info.Protect == PAGE_READONLY, "protection restored");
    const char* why = nc::PatchQword(slot, 0x140BF6420ull, 1);
    Check(why && std::strcmp(why, "changed") == 0 && *slot == 0x7FF600001234ull, "CAS refusal leaves the value");
    why = nc::PatchQword(static_cast<unsigned char*>(page) + 0x29, 0, 1);
    Check(why && std::strcmp(why, "alignment") == 0, "unaligned refused");
    VirtualFree(page, 0, MEM_RELEASE);
}

// detail::RetargetCall (shared with partner_defer's H2): near stub + rel32 swap.
void TestRetargetCall() {
    void* page = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    Check(page != nullptr, "code page allocated");
    auto* site = static_cast<unsigned char*>(page) + 0x40;
    const std::uint32_t orig = 0x000001C6u;
    site[0] = 0xE8;
    std::memcpy(site + 1, &orig, 4);
    const auto site_va = reinterpret_cast<std::uintptr_t>(site);
    const void* target = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x1122334455667788ull));
    void* stub = nullptr;
    Check(std::strcmp(hk::detail::RetargetCall(site_va, site_va + 5, orig + 1, target, stub), "bytes") == 0 && !stub,
          "unexpected rel32 refused before any allocation");
    Check(hk::detail::RetargetCall(site_va, site_va + 5, orig, target, stub) == nullptr && stub, "retarget succeeds");
    std::int32_t rel = 0;
    std::memcpy(&rel, site + 1, 4);
    Check(site[0] == 0xE8 && site_va + 5 + static_cast<std::intptr_t>(rel) == reinterpret_cast<std::uintptr_t>(stub),
          "rel32 now reaches the stub");
    const auto* s = static_cast<const unsigned char*>(stub);
    std::uintptr_t abs = 0;
    std::memcpy(&abs, s + 6, 8);
    Check(s[0] == 0xFF && s[1] == 0x25 && abs == reinterpret_cast<std::uintptr_t>(target), "stub jumps to the target");
    void* again = nullptr;
    Check(std::strcmp(hk::detail::RetargetCall(site_va, site_va + 5, orig, target, again), "bytes") == 0, "second retarget refused");
    VirtualFree(page, 0, MEM_RELEASE);
}

} // namespace

int main() {
    TestStubBytes();
    TestRel32();
    TestBlockGate();
    TestPatchDword();
    TestKmSlots();
    TestPatchQword();
    TestRetargetCall();
    std::printf("%u partner input hook checks passed: stub bytes, rel32 reach/cache-line, dispatch-block gate "
                "(mismatch + retargeted rel32), locked dword CAS (success/refusal/split), key-manager slot map, "
                "data qword swap\n", checks);
    return 0;
}
