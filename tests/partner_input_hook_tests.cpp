// Round 3 input hook: stub bytes, rel32 reach, the dispatch-block gate and the
// locked dword patcher, on synthetic memory (no game, no real install).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "partner_player_contract.hpp"

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

// Native weapons (docs/native-puppet.md "Weapons"): the RequestBullet owner
// filter on a synthetic image holding the contract bytes and both vtable
// slots. The game calls slot 0 with (iface, record); record+0x2A is the owner.
namespace ff = mgscoop::partner_fire_filter;
namespace pc = mgscoop::partner_player_contract;
unsigned g_forwarded = 0;
const void* g_forwarded_record = nullptr;
void* FakeRequestBullet(void* iface, const void* record) noexcept {
    ++g_forwarded;
    g_forwarded_record = record;
    return iface;
}
std::string g_fire_log;
void FireLog(const char* line) noexcept { g_fire_log += line; }
struct FireImage {
    unsigned char* mem = nullptr;
    std::uintptr_t base = 0;
    std::size_t span = (pc::service_root_rva > pc::fire_slot_live_rva ? pc::service_root_rva : pc::fire_slot_live_rva) + 0x1000;
    FireImage() {
        mem = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!mem) { std::fprintf(stderr, "fire image allocation failed\n"); std::exit(2); }
        base = reinterpret_cast<std::uintptr_t>(mem);
        for (const auto& s : pc::code_sites)
            if (s.rva == pc::fire_request_rva) std::memcpy(mem + s.rva, s.bytes, s.size);
        const std::uintptr_t target = base + pc::fire_request_rva;
        std::memcpy(mem + pc::fire_slot_live_rva, &target, 8);
        std::memcpy(mem + pc::fire_slot_dying_rva, &target, 8);
    }
    ~FireImage() { VirtualFree(mem, 0, MEM_RELEASE); }
    std::uintptr_t Slot(std::uintptr_t rva) const { std::uintptr_t v = 0; std::memcpy(&v, mem + rva, 8); return v; }
    // What the game does at 0x13031BB: rax = *(iface); call [rax] (slot 0).
    void* Call(std::uintptr_t slot_rva, std::uint16_t owner) const {
        alignas(16) unsigned char record[0x60]{};
        std::memcpy(record + pc::shot_owner, &owner, 2);
        const auto fn = reinterpret_cast<ff::detail::RequestFn>(Slot(slot_rva));
        return fn(reinterpret_cast<void*>(0x1234), record);
    }
};
void TestFireFilter() {
    Check(ff::detail::Drops(1, 1) && !ff::detail::Drops(1, 0) && !ff::detail::Drops(1, 0x401) && !ff::detail::Drops(0xFFFF, 1) &&
          !ff::detail::Drops(0x201, 0x201), "Drops: only the armed player id");
    {
        // A changed RequestBullet prologue refuses; nothing is swapped.
        FireImage img;
        ff::detail::ResetForTest();
        img.mem[pc::fire_request_rva + 5] ^= 1;
        g_fire_log.clear();
        const char* why = ff::Install(reinterpret_cast<void*>(img.base), &FireLog);
        Check(why && std::strcmp(why, "bytes") == 0 && !ff::Installed() && img.Slot(pc::fire_slot_live_rva) == img.base + pc::fire_request_rva &&
              g_fire_log.find("partner_fire=refused; reason=bytes; site=0xd32c60; fire=masked") != std::string::npos,
              "prologue mismatch: refused, slots untouched, logged");
        Check(std::strcmp(ff::Install(reinterpret_cast<void*>(img.base), &FireLog), "disabled") == 0, "a failed install stays refused");
    }
    {
        // A slot that does not hold RequestBullet refuses.
        FireImage img;
        ff::detail::ResetForTest();
        const std::uintptr_t other = img.base + 0x1000;
        std::memcpy(img.mem + pc::fire_slot_dying_rva, &other, 8);
        const char* why = ff::Install(reinterpret_cast<void*>(img.base), &FireLog);
        Check(why && std::strcmp(why, "slot") == 0 && img.Slot(pc::fire_slot_live_rva) == img.base + pc::fire_request_rva,
              "slot mismatch: refused, the live slot untouched");
    }
    {
        FireImage img;
        ff::detail::ResetForTest();
        ff::detail::SetOriginalForTest(&FakeRequestBullet);
        g_fire_log.clear();
        Check(ff::Install(reinterpret_cast<void*>(img.base), &FireLog) == nullptr && ff::Installed() &&
              ff::PatchLive(reinterpret_cast<void*>(img.base)), "install on the contract bytes");
        const auto hook = reinterpret_cast<std::uintptr_t>(ff::detail::HookEntry());
        Check(img.Slot(pc::fire_slot_live_rva) == hook && img.Slot(pc::fire_slot_dying_rva) == hook &&
              g_fire_log.find("partner_fire=installed; slots=0x2337de8,0x2337d18; target=0xd32c60") != std::string::npos,
              "both slots hold the filter");
        Check(ff::Install(reinterpret_cast<void*>(img.base), &FireLog) == nullptr, "a second install is idempotent");
        // Disarmed: everything is forwarded (owner 1 counted).
        g_forwarded = 0;
        Check(img.Call(pc::fire_slot_live_rva, 1) == reinterpret_cast<void*>(0x1234) && g_forwarded == 1, "disarmed: forwarded");
        Check(!ff::Arm(0x401) && !ff::Armed(), "a non-player owner cannot be armed");
        Check(ff::Arm(1) && ff::Armed(), "armed for instance 1");
        Check(img.Call(pc::fire_slot_live_rva, 1) == nullptr && g_forwarded == 1, "owner 1: dropped, returns 0, never forwarded");
        Check(img.Call(pc::fire_slot_dying_rva, 1) == nullptr && g_forwarded == 1, "owner 1 through the destruction vtable: dropped");
        Check(img.Call(pc::fire_slot_live_rva, 0) == reinterpret_cast<void*>(0x1234) && g_forwarded == 2, "owner 0 (the local player): forwarded");
        Check(img.Call(pc::fire_slot_live_rva, 0x401) == reinterpret_cast<void*>(0x1234) && g_forwarded == 3 && g_forwarded_record,
              "owner 0x401 (a soldier with index 1): forwarded with the same record");
        ff::Stats st{};
        ff::ReadStats(reinterpret_cast<void*>(img.base), st);
        Check(st.installed && st.live && st.armed && st.owner == 1 && st.suppressed == 2 && st.passed == 3 && st.passed_player == 2 &&
              st.owner_disarmed == 0 && st.last_owner == 0x401 && st.last_thread == GetCurrentThreadId(),
              "stats: suppressed 2, passed 3 (2 player-owned), last owner and thread");
        ff::Disarm();
        Check(img.Call(pc::fire_slot_live_rva, 1) == reinterpret_cast<void*>(0x1234), "disarmed again: owner 1 forwarded");
        ff::ReadStats(reinterpret_cast<void*>(img.base), st);
        Check(!st.armed && st.owner_disarmed == 1, "owner 1 forwarded while disarmed is counted");
        // A slot restored behind our back is not live.
        const std::uintptr_t target = img.base + pc::fire_request_rva;
        std::memcpy(img.mem + pc::fire_slot_dying_rva, &target, 8);
        Check(!ff::PatchLive(reinterpret_cast<void*>(img.base)), "a restored slot: not live");
        // The census reads the service chain only (no call): root -> svc -> bullets / noise.
        alignas(16) static unsigned char svc[0x300], gm[0x300], bullets[0x40], noise_owner[0x40], noise[0x40], root[0x40];
        alignas(16) static std::uintptr_t noise_vt[4];
        std::memset(svc, 0, sizeof(svc));
        const auto put = [](void* at, std::uintptr_t v) { std::memcpy(at, &v, 8); };
        put(img.mem + pc::service_root_rva, reinterpret_cast<std::uintptr_t>(root));
        put(root + pc::root_service, reinterpret_cast<std::uintptr_t>(svc));
        put(svc + pc::svc_gm, reinterpret_cast<std::uintptr_t>(gm));
        put(gm + pc::gm_bullets, reinterpret_cast<std::uintptr_t>(bullets));
        put(bullets, img.base + pc::fire_slot_live_rva);
        put(svc + pc::svc_noise_owner, reinterpret_cast<std::uintptr_t>(noise_owner));
        put(noise_owner + pc::noise_owner_iface, reinterpret_cast<std::uintptr_t>(noise));
        noise_vt[0] = img.base + 0x500;
        put(noise, reinterpret_cast<std::uintptr_t>(noise_vt));
        ff::Census cs{};
        ff::ReadCensus(reinterpret_cast<void*>(img.base), img.span, cs);
        Check(cs.ok && cs.bullets_ok && cs.bullets_vt == pc::fire_slot_live_rva && !cs.noise_vt_in_image && cs.noise_fn0_in_image &&
              cs.noise_fn0 == 0x500 && cs.noise_vt == reinterpret_cast<std::uintptr_t>(noise_vt), "census: bullets vtable verified, noise vtable reported");
        put(img.mem + pc::service_root_rva, 0);
        ff::ReadCensus(reinterpret_cast<void*>(img.base), img.span, cs);
        Check(!cs.ok, "census without a service root: not ok");
        ff::Disarm();
        ff::detail::SetOriginalForTest(nullptr);
        ff::detail::ResetForTest();
    }
}

} // namespace

int main() {
    TestFireFilter();
    TestStubBytes();
    TestRel32();
    TestBlockGate();
    TestPatchDword();
    TestKmSlots();
    TestPatchQword();
    TestRetargetCall();
    std::printf("%u partner input hook checks passed: stub bytes, rel32 reach/cache-line, dispatch-block gate "
                "(mismatch + retargeted rel32), locked dword CAS (success/refusal/split), key-manager slot map, "
                "data qword swap, fire filter (prologue/slot refusals, install, owner filter, stats, live check, census)\n", checks);
    return 0;
}
