// Round 3 input hook: stub bytes, rel32 reach, the dispatch-block gate and the
// locked dword patcher, on synthetic memory (no game, no real install).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/partner_look_source.hpp"
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

// M7 seated shots, S1 (partner_seat_shot.hpp): the RequestBullet hook forwards a moved COPY of a seated passenger's
// record (never the caller's record: the shot handler re-uses it for every pellet), only for the published owner, only
// with a fresh publication and apply on.
std::uint64_t g_ss_now = 1000;
std::uint64_t SeatShotClock() noexcept { return g_ss_now; }
alignas(16) unsigned char g_ss_seen_record[0x40]{};
void* SeatShotFakeRequest(void* iface, const void* record) noexcept {
    ++g_forwarded;
    g_forwarded_record = record;
    std::memcpy(g_ss_seen_record, record, mgscoop::partner_seat_shot::kRecordBytes);
    return iface;
}
void TestSeatShotHook() {
    namespace ss = mgscoop::partner_seat_shot;
    FireImage img;
    ff::detail::ResetForTest();
    ff::detail::SetOriginalForTest(&SeatShotFakeRequest);
    ff::detail::SetSeatShotClockForTest(&SeatShotClock);
    Check(ff::Install(reinterpret_cast<void*>(img.base), &FireLog) == nullptr && ff::Arm(1), "S1 hook: installed, instance 1 armed");
    const auto fn = reinterpret_cast<ff::detail::RequestFn>(img.Slot(pc::fire_slot_live_rva));
    // A seated shot across a jeep at yaw 0 (the pure test's geometry): start 1 m back, 0.3 m right, 1.1 m up; the guard
    // 15.3 m to the left.
    alignas(16) unsigned char record[0x60]{};
    const float start[4] = {99.7f, 11.1f, 199.0f, 1.0f};
    float dir[4] = {15.3f, -0.9f, 0.0f, 0.0f};
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
    dir[0] /= len;
    dir[1] /= len;
    const auto fill = [&](std::uint16_t owner) {
        std::memset(record, 0, sizeof(record));
        std::memcpy(record + ss::kPos, start, sizeof(start));
        std::memcpy(record + ss::kDir, dir, sizeof(dir));
        std::memcpy(record + pc::shot_owner, &owner, 2);
        record[0x2E] = 0x12;
        record[0x32] = 0x45;
    };
    // Nothing published: the caller's record is forwarded as it is.
    g_forwarded = 0;
    fill(0);
    Check(fn(reinterpret_cast<void*>(0x1234), record) == reinterpret_cast<void*>(0x1234) && g_forwarded == 1 && g_forwarded_record == record &&
              !ff::SeatShotPublished(),
          "S1 hook: unpublished: the record itself");
    ss::Pub p{};
    p.owner = 0;
    p.vehicle = 25608;
    p.seat = 3;
    p.cls = ss::Class::light;
    p.apply = true;
    p.target_ok = true;
    p.pos[0] = 100.0f; p.pos[1] = 10.0f; p.pos[2] = 200.0f;
    p.target[0] = 115.0f; p.target[1] = 10.2f; p.target[2] = 199.0f;
    ff::PublishSeatShot(p);
    Check(ff::SeatShotPublished(), "S1 hook: published");
    // The local passenger's shot: a moved copy is forwarded; the caller's record is unchanged.
    fill(0);
    unsigned char before[0x60];
    std::memcpy(before, record, sizeof(before));
    Check(fn(reinterpret_cast<void*>(0x1234), record) == reinterpret_cast<void*>(0x1234) && g_forwarded == 2 && g_forwarded_record != record &&
              std::memcmp(before, record, sizeof(before)) == 0,
          "S1 hook: a copy is forwarded, the caller's record untouched");
    float moved[4], kept[4];
    std::memcpy(moved, g_ss_seen_record + ss::kPos, sizeof(moved));
    std::memcpy(kept, g_ss_seen_record + ss::kDir, sizeof(kept));
    Check(moved[0] > 101.0f && moved[0] < 101.1f && std::fabs(moved[2] - 199.0f) < 1e-3f && moved[3] == 1.0f &&
              std::memcmp(kept, dir, sizeof(dir)) == 0 && std::memcmp(g_ss_seen_record + 0x20, record + 0x20, ss::kRecordBytes - 0x20) == 0,
          "S1 hook: the start moved past the jeep's far side; the direction, w and the rest of the record kept");
    ff::SeatShotStats st{};
    Check(ff::ReadSeatShot(st) && st.seq == 1 && st.shifted == 1 && st.seen == 1 && std::strcmp(st.result, "shift") == 0 && st.inside &&
              st.vehicle == 25608 && st.seat == 3 && st.apply && st.t > 1.34f && st.t < 1.37f,
          "S1 hook: the last plan and the counters");
    // Pellets: the same record with a new direction each time: each moves along its own direction.
    {
        float d2[4] = {dir[0], dir[1], 0.05f, 0.0f};
        std::memcpy(record + ss::kDir, d2, sizeof(d2));
        fn(reinterpret_cast<void*>(0x1234), record);
        float m2[4];
        std::memcpy(m2, g_ss_seen_record + ss::kPos, sizeof(m2));
        Check(std::fabs(m2[2] - 199.0f) > 0.05f && std::memcmp(record + ss::kPos, start, sizeof(start)) == 0,
              "S1 hook: a second pellet moves along its own direction; the record's start still the original");
    }
    // Instance 1 (the armed partner body) is still dropped; a soldier owner (type bits) and another player are untouched.
    const unsigned forwarded = g_forwarded;
    fill(1);
    Check(fn(reinterpret_cast<void*>(0x1234), record) == nullptr && g_forwarded == forwarded, "S1 hook: instance 1 still dropped");
    fill(0x401);
    Check(fn(reinterpret_cast<void*>(0x1234), record) && g_forwarded_record == record, "S1 hook: a soldier's shot untouched");
    fill(2);
    Check(fn(reinterpret_cast<void*>(0x1234), record) && g_forwarded_record == record, "S1 hook: another owner untouched");
    // Apply off: planned (census), the record forwarded as it is.
    p.apply = false;
    ff::PublishSeatShot(p);
    fill(0);
    Check(fn(reinterpret_cast<void*>(0x1234), record) && g_forwarded_record == record && ff::ReadSeatShot(st) &&
              std::strcmp(st.result, "census") == 0 && st.census == 1 && !st.apply,
          "S1 hook: apply off: census only");
    // A stale publication (older than kFreshMs): untouched, counted. 60 ms is already stale: at 20 m/s a jeep moves
    // 1.2 m in that time (review of 8.30: 500 ms let a moving jeep's box lie metres ahead).
    p.apply = true;
    ff::PublishSeatShot(p);
    g_ss_now += 60;
    fill(0);
    Check(fn(reinterpret_cast<void*>(0x1234), record) && g_forwarded_record == record && ff::ReadSeatShot(st) && st.stale == 1,
          "S1 hook: a publication 60 ms old: stale, untouched");
    ff::PublishSeatShot(p);
    g_ss_now += ss::kFreshMs + 1;
    fill(0);
    Check(fn(reinterpret_cast<void*>(0x1234), record) && g_forwarded_record == record && ff::ReadSeatShot(st) && st.stale == 2,
          "S1 hook: a stale publication: untouched");
    // Fresh again, but the line misses the box (shooting straight up): untouched, refused counted.
    ff::PublishSeatShot(p);
    fill(0);
    const float up[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    std::memcpy(record + ss::kDir, up, sizeof(up));
    const float high[4] = {100.0f, 14.0f, 200.0f, 1.0f};
    std::memcpy(record + ss::kPos, high, sizeof(high));
    Check(fn(reinterpret_cast<void*>(0x1234), record) && g_forwarded_record == record && ff::ReadSeatShot(st) &&
              std::strcmp(st.result, "miss") == 0 && st.refused == 1,
          "S1 hook: a line that misses the box: untouched");
    // Withdrawn: untouched again, nothing counted.
    ff::WithdrawSeatShot();
    fill(0);
    const std::uint32_t seen = st.seen;
    Check(fn(reinterpret_cast<void*>(0x1234), record) && g_forwarded_record == record && !ff::SeatShotPublished() && ff::ReadSeatShot(st) &&
              st.seen == seen,
          "S1 hook: withdrawn: untouched");
    // The lever's override.
    Check(ff::SeatShotOverride() == -1, "S1 lever: none by default");
    ff::SetSeatShotOverride(1);
    Check(ff::SeatShotOverride() == 1, "S1 lever: on");
    ff::SetSeatShotOverride(-5);
    Check(ff::SeatShotOverride() == -1, "S1 lever: cleared");
    ff::Disarm();
    ff::detail::SetSeatShotClockForTest(nullptr);
    ff::detail::SetOriginalForTest(nullptr);
    ff::detail::ResetForTest();
}

// Anim round 1 (docs/anim-round1-weapons.md): the fire/projectile census. The
// bullet hook records every request with its owner and record fields before its
// own verdict; with the census on, Install also swaps four forwarding slots
// (projectile, rocket, thrown, placed) in their services' live and base iface
// vtables.
unsigned g_slot_forwarded[ff::kCensusSlots]{};
template <unsigned I>
void* FakeSlotRequest(void* iface, const void*) noexcept {
    ++g_slot_forwarded[I];
    return iface;
}
struct SlotSpec {
    std::uintptr_t target, live, base;
    std::size_t offset, id, aux;
};
const SlotSpec kSlotSpecs[ff::kCensusSlots] = {
    {pc::proj_request_rva, pc::proj_slot_live_rva, pc::proj_slot_base_rva, 0, pc::proj_id, pc::proj_aux},
    {pc::rocket_request_rva, pc::proj_slot_live_rva, pc::proj_slot_base_rva, pc::rocket_slot, pc::rocket_id, pc::rocket_aux},
    {pc::thrown_request_rva, pc::thrown_slot_live_rva, pc::thrown_slot_base_rva, pc::thrown_slot, pc::thrown_id, pc::thrown_aux},
    {pc::placed_request_rva, pc::placed_slot_live_rva, pc::placed_slot_base_rva, pc::placed_slot, pc::placed_id, pc::placed_aux},
};
void PrepareCensusSlots(FireImage& img) {
    for (const SlotSpec& k : kSlotSpecs) {
        for (const auto& s : pc::code_sites)
            if (s.rva == k.target) std::memcpy(img.mem + s.rva, s.bytes, s.size);
        const std::uintptr_t t = img.base + k.target;
        std::memcpy(img.mem + k.live + k.offset, &t, 8);
        std::memcpy(img.mem + k.base + k.offset, &t, 8);
    }
}
void TestFireCensus() {
    static_assert(ff::kCensusSlots == 4, "projectile, rocket, thrown, placed");
    {
        FireImage img;
        Check(img.span > pc::thrown_slot_live_rva + 0x10 && img.span > pc::placed_slot_live_rva + 0x50, "the synthetic image spans the slots");
        ff::detail::ResetForTest();
        PrepareCensusSlots(img);
        ff::detail::SetOriginalForTest(&FakeRequestBullet);
        ff::detail::SetSlotOriginalForTest(0, &FakeSlotRequest<0>);
        ff::detail::SetSlotOriginalForTest(1, &FakeSlotRequest<1>);
        ff::detail::SetSlotOriginalForTest(2, &FakeSlotRequest<2>);
        ff::detail::SetSlotOriginalForTest(3, &FakeSlotRequest<3>);
        // Census off: Install leaves the census slots alone.
        g_fire_log.clear();
        Check(ff::Install(reinterpret_cast<void*>(img.base), &FireLog) == nullptr &&
                  img.Slot(pc::proj_slot_live_rva) == img.base + pc::proj_request_rva &&
                  g_fire_log.find("partner_fire_census") == std::string::npos, "census off: the census slots untouched");
        ff::SetCensus(true);
        Check(ff::CensusOn() && ff::Install(reinterpret_cast<void*>(img.base), &FireLog) == nullptr, "census on: the next install call");
        for (unsigned i = 0; i < ff::kCensusSlots; ++i) {
            const auto hook = reinterpret_cast<std::uintptr_t>(ff::detail::SlotHookEntry(i));
            Check(img.Slot(kSlotSpecs[i].live + kSlotSpecs[i].offset) == hook && img.Slot(kSlotSpecs[i].base + kSlotSpecs[i].offset) == hook,
                  "census on: both vtable slots of every census slot hold its hook");
        }
        Check(g_fire_log.find("partner_fire_census=slot_installed; kind=projectile; slots=0x23e2d50+0x0,0x23e2bd0+0x0; target=0x1337760; "
                              "owner_field=0x32") != std::string::npos &&
                  g_fire_log.find("partner_fire_census=slot_installed; kind=thrown; slots=0x2429ad0+0x8,0x24299a0+0x8; target=0x15e0230") !=
                      std::string::npos &&
                  g_fire_log.find("kind=placed; slots=0x23a9fd0+0x48,0x23a9d20+0x48; target=0xfccd80") != std::string::npos &&
                  g_fire_log.find("kind=rocket; slots=0x23e2d50+0x58,0x23e2bd0+0x58; target=0x1337920") != std::string::npos,
              "census on: one installed line per slot");
        // The bullet filter's own slots are not the projectile slot 0 (another vtable): untouched by the census.
        ff::CensusTotals t{};
        ff::ReadCensusTotals(t);
        for (unsigned i = 0; i < ff::kCensusSlots; ++i)
            Check(t.slot_installed[i] && t.slot_live[i] && !t.slot_refusal[i], "census totals: every slot installed and live");
        std::uint32_t cursor = 0, lost = 0;
        ff::CensusEntry e[16]{};
        Check(ff::DrainCensus(cursor, e, 16, lost) == 0 && lost == 0, "nothing recorded yet");
        // A bullet of the local instance (owner 0) and of instance 1 (owner 1, armed: dropped).
        Check(ff::Arm(1), "armed for instance 1");
        const auto shot = [&](std::uint16_t owner, std::uint16_t id, std::uint8_t kind) {
            alignas(16) unsigned char record[0x60]{};
            std::memcpy(record + pc::shot_owner, &owner, 2);
            std::memcpy(record + pc::shot_bullet_id, &id, 2);
            record[pc::shot_kind] = kind;
            record[pc::shot_sub] = 2;
            record[pc::shot_flags] = 4;
            return reinterpret_cast<ff::detail::RequestFn>(img.Slot(pc::fire_slot_live_rva))(reinterpret_cast<void*>(0x1234), record);
        };
        g_forwarded = 0;
        Check(shot(0, 0x123, 7) == reinterpret_cast<void*>(0x1234) && shot(1, 0x124, 7) == nullptr && g_forwarded == 1,
              "census: the filter's verdicts are unchanged (owner 0 forwarded, owner 1 dropped)");
        // One request per census slot (owner 1: the census never drops); each forwards with its return value.
        for (unsigned i = 0; i < ff::kCensusSlots; ++i) {
            alignas(16) unsigned char request[0x50]{};
            const std::uint16_t owner = 1, id = static_cast<std::uint16_t>(0x70 + i), aux = static_cast<std::uint16_t>(9 + i);
            std::memcpy(request + pc::req_owner, &owner, 2);
            std::memcpy(request + kSlotSpecs[i].id, &id, 2);
            std::memcpy(request + kSlotSpecs[i].aux, &aux, 2);
            g_slot_forwarded[i] = 0;
            const auto fn = reinterpret_cast<ff::detail::ProjectileFn>(img.Slot(kSlotSpecs[i].live + kSlotSpecs[i].offset));
            Check(fn(reinterpret_cast<void*>(0x4321), request) == reinterpret_cast<void*>(0x4321) && g_slot_forwarded[i] == 1,
                  "a census slot forwards every request (rax included), owner 1 too");
        }
        ff::detail::RecordForTest(ff::CensusKind::noise, false, 0, 0x45);
        const std::size_t n = ff::DrainCensus(cursor, e, 16, lost);
        Check(n == 7 && lost == 0 && cursor == 7, "seven entries drained in order");
        Check(e[0].kind == ff::CensusKind::bullet && e[0].owner == 0 && e[0].id == 0x123 && e[0].b0 == 7 && e[0].b1 == 2 && e[0].b2 == 4 &&
                  !e[0].dropped && e[0].fields_ok && e[0].thread == GetCurrentThreadId() && e[0].seq == 1,
              "bullet entry: owner 0, bullet id, kind/sub/flags, forwarded");
        Check(e[1].kind == ff::CensusKind::bullet && e[1].owner == 1 && e[1].dropped, "bullet entry: owner 1 dropped");
        const ff::CensusKind kinds[4] = {ff::CensusKind::projectile, ff::CensusKind::rocket, ff::CensusKind::thrown, ff::CensusKind::placed};
        for (unsigned i = 0; i < 4; ++i)
            Check(e[2 + i].kind == kinds[i] && e[2 + i].owner == 1 && e[2 + i].id == 0x70 + i && e[2 + i].aux == 9 + i && !e[2 + i].dropped &&
                      e[2 + i].fields_ok, "census slot entry: kind, owner +0x32, id and aux fields (never dropped)");
        Check(e[6].kind == ff::CensusKind::noise && e[6].id == 0x45 && std::strcmp(ff::CensusKindName(ff::CensusKind::thrown), "thrown") == 0 &&
                  std::strcmp(ff::CensusKindName(ff::CensusKind::placed), "placed") == 0, "noise entry, names");
        // Overrun: older entries are lost, never torn.
        for (unsigned i = 0; i < ff::kCensusRing + 44; ++i) ff::detail::RecordForTest(ff::CensusKind::bullet, false, 0, static_cast<std::uint16_t>(i));
        ff::CensusEntry big[ff::kCensusRing]{};
        std::size_t got = 0;
        std::uint32_t lost_total = 0;
        for (int k = 0; k < 8; ++k) {
            got += ff::DrainCensus(cursor, big, ff::kCensusRing, lost);
            lost_total += lost;
        }
        Check(lost_total == 44 && got == ff::kCensusRing && big[ff::kCensusRing - 1].id == ff::kCensusRing + 43,
              "an overrun drains the newest ring and counts the lost ones");
        ff::CensusTotals t2{};
        ff::ReadCensusTotals(t2);
        Check(t2.bullets == 2 && t2.slot_requests[0] == 1 && t2.slot_requests[2] == 1 && t2.recorded == 7 + ff::kCensusRing + 44,
              "census totals count the hooks' records");
        ff::SetCensus(false);
        shot(0, 1, 1);
        Check(ff::DrainCensus(cursor, e, 16, lost) == 0, "census off: nothing recorded");
        ff::Disarm();
        ff::detail::SetOriginalForTest(nullptr);
        for (unsigned i = 0; i < ff::kCensusSlots; ++i) ff::detail::SetSlotOriginalForTest(i, nullptr);
        ff::detail::ResetForTest();
    }
    {
        // A changed thrown-request prologue: that slot refuses (untouched), the others install, the bullet filter too.
        FireImage img;
        ff::detail::ResetForTest();
        PrepareCensusSlots(img);
        img.mem[pc::thrown_request_rva + 2] ^= 1;
        // A placed base slot not holding the request: refused before any swap.
        const std::uintptr_t other = img.base + 0x3000;
        std::memcpy(img.mem + pc::placed_slot_base_rva + pc::placed_slot, &other, 8);
        ff::SetCensus(true);
        g_fire_log.clear();
        Check(ff::Install(reinterpret_cast<void*>(img.base), &FireLog) == nullptr, "the bullet filter installs");
        Check(img.Slot(pc::thrown_slot_live_rva + pc::thrown_slot) == img.base + pc::thrown_request_rva &&
                  g_fire_log.find("partner_fire_census=slot_refused; kind=thrown; reason=bytes") != std::string::npos,
              "thrown prologue mismatch: refused, untouched");
        Check(img.Slot(pc::placed_slot_live_rva + pc::placed_slot) == img.base + pc::placed_request_rva &&
                  img.Slot(pc::placed_slot_base_rva + pc::placed_slot) == other &&
                  g_fire_log.find("partner_fire_census=slot_refused; kind=placed; reason=slot_base") != std::string::npos,
              "a placed base slot without the request: refused before any swap");
        ff::CensusTotals t{};
        ff::ReadCensusTotals(t);
        Check(t.slot_installed[0] && t.slot_installed[1] && !t.slot_installed[2] && !t.slot_installed[3] &&
                  std::strcmp(t.slot_refusal[2], "bytes") == 0 && std::strcmp(t.slot_refusal[3], "slot_base") == 0,
              "census totals: per-slot verdicts");
        ff::SetCensus(false);
        ff::detail::ResetForTest();
    }
}

// Native polish F3: the shot noise filter (AddNoise 0x515270 through slot 0 of
// the noise iface vtable 0x21876A8; the record keeps its owner at +0x12).
unsigned g_noise_forwarded = 0;
void FakeAddNoise(void*, const void*) noexcept { ++g_noise_forwarded; }
void TestNoiseFilter() {
    {
        FireImage img;
        ff::detail::ResetForTest();
        for (const auto& s : pc::code_sites)
            if (s.rva == pc::noise_fn_rva) std::memcpy(img.mem + s.rva, s.bytes, s.size);
        const std::uintptr_t target = img.base + pc::noise_fn_rva;
        std::memcpy(img.mem + pc::noise_vtable_rva, &target, 8);
        img.mem[pc::noise_fn_rva + 3] ^= 1;
        g_fire_log.clear();
        const char* why = ff::InstallNoise(reinterpret_cast<void*>(img.base), &FireLog);
        Check(why && std::strcmp(why, "bytes") == 0 && !ff::NoiseInstalled() && img.Slot(pc::noise_vtable_rva) == target &&
                  g_fire_log.find("partner_noise=refused; reason=bytes; fire=masked") != std::string::npos,
              "noise: a changed AddNoise prologue refuses, the slot untouched");
        Check(std::strcmp(ff::InstallNoise(reinterpret_cast<void*>(img.base), &FireLog), "disabled") == 0, "noise: a failed install stays refused");
    }
    {
        FireImage img;
        ff::detail::ResetForTest();
        for (const auto& s : pc::code_sites)
            if (s.rva == pc::noise_fn_rva) std::memcpy(img.mem + s.rva, s.bytes, s.size);
        const std::uintptr_t other = img.base + 0x2000;
        std::memcpy(img.mem + pc::noise_vtable_rva, &other, 8);
        Check(std::strcmp(ff::InstallNoise(reinterpret_cast<void*>(img.base), &FireLog), "slot") == 0 && img.Slot(pc::noise_vtable_rva) == other,
              "noise: a slot that does not hold AddNoise refuses");
    }
    {
        FireImage img;
        ff::detail::ResetForTest();
        for (const auto& s : pc::code_sites)
            if (s.rva == pc::noise_fn_rva) std::memcpy(img.mem + s.rva, s.bytes, s.size);
        const std::uintptr_t target = img.base + pc::noise_fn_rva;
        std::memcpy(img.mem + pc::noise_vtable_rva, &target, 8);
        ff::detail::SetNoiseOriginalForTest(&FakeAddNoise);
        g_fire_log.clear();
        Check(ff::InstallNoise(reinterpret_cast<void*>(img.base), &FireLog) == nullptr && ff::NoiseInstalled() &&
                  ff::NoisePatchLive(reinterpret_cast<void*>(img.base)) &&
                  img.Slot(pc::noise_vtable_rva) == reinterpret_cast<std::uintptr_t>(ff::detail::NoiseHookEntry()) &&
                  g_fire_log.find("partner_noise=installed; slot=0x21876a8; target=0x515270; owner_field=0x12") != std::string::npos,
              "noise: installed into slot 0");
        const auto call = [&](std::uint16_t owner) {
            alignas(16) unsigned char record[0x20]{};
            record[0x10] = 0x45; record[0x11] = 1;
            std::memcpy(record + pc::noise_owner, &owner, 2);
            reinterpret_cast<ff::detail::NoiseFn>(img.Slot(pc::noise_vtable_rva))(reinterpret_cast<void*>(0x5678), record);
        };
        g_noise_forwarded = 0;
        call(1);
        Check(g_noise_forwarded == 1, "noise: disarmed, owner 1 forwarded");
        Check(ff::Arm(1), "armed for instance 1 (bullets and noise)");
        call(1);
        call(0);
        call(0x401);
        ff::Stats st{};
        ff::ReadStats(reinterpret_cast<void*>(img.base), st);
        Check(g_noise_forwarded == 3 && st.noise_installed && st.noise_live && st.noise_dropped == 1 && st.noise_passed == 3 &&
                  st.noise_last_owner == 0x401, "noise: owner 1 dropped, the local player and soldiers forwarded");
        std::memcpy(img.mem + pc::noise_vtable_rva, &target, 8);
        Check(!ff::NoisePatchLive(reinterpret_cast<void*>(img.base)), "noise: a restored slot is not live");
        ff::Disarm();
        ff::detail::SetNoiseOriginalForTest(nullptr);
        ff::detail::ResetForTest();
    }
}

// Native polish D3: the look source hook on a synthetic executable image
// holding the contract bytes of PS phase 0's look call. After the install the
// padding jump runs the near stub: the "game" look computation (a fake at
// 0xFFAC00 writing the viewer's look), then the remote's look for instance 1
// only while published on this thread. The stub's unwind data lets an
// exception raised inside 0xFFAC00 reach a handler above it.
namespace ls = mgscoop::partner_look_source;
std::string g_look_log;
void LookLog(const char* line) noexcept { g_look_log += line; }
struct LookImage {
    unsigned char* mem = nullptr;
    std::uintptr_t base = 0;
    std::size_t span = pc::look_pad_end_rva + 0x1000;
    LookImage() {
        mem = static_cast<unsigned char*>(VirtualAlloc(nullptr, span, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        if (!mem) { std::fprintf(stderr, "look image allocation failed\n"); std::exit(2); }
        base = reinterpret_cast<std::uintptr_t>(mem);
        for (const auto& s : pc::code_sites)
            if (s.rva >= pc::look_src_call_block_rva && s.rva < pc::look_pad_end_rva) std::memcpy(mem + s.rva, s.bytes, s.size);
        std::memset(mem + pc::look_pad_rva, 0xCC, pc::look_pad_end_rva - pc::look_pad_rva);
    }
    ~LookImage() { VirtualFree(mem, 0, MEM_RELEASE); }
    std::uint64_t Qword() const { std::uint64_t v = 0; std::memcpy(&v, mem + pc::look_src_qword_rva, 8); return v; }
};
struct FakePs {
    std::uintptr_t vt;
    std::uint32_t base_id;
    unsigned char pad[0x280 - 0xC];
    std::uintptr_t rec;      // +0x280
    unsigned char pad2[8];
    std::uintptr_t look;     // +0x290
};
static_assert(offsetof(FakePs, rec) == 0x280 && offsetof(FakePs, look) == 0x290, "fake PS layout");
float g_look_pairs[2][2]{};
std::uint32_t g_look_recs[2][8]{};
// mov rax, &g_look_pairs[1]; mov dword [rax], viewer pitch; mov dword [rax+4], viewer yaw; ret
void WriteFakeLook(unsigned char* at) {
    const auto slot = reinterpret_cast<std::uintptr_t>(&g_look_pairs[1][0]);
    const float pitch = 0.25f, yaw = -1.5f;
    unsigned char code[24] = {0x48, 0xB8};
    std::memcpy(code + 2, &slot, 8);
    code[10] = 0xC7; code[11] = 0x00; std::memcpy(code + 12, &pitch, 4);
    code[16] = 0xC7; code[17] = 0x40; code[18] = 0x04; std::memcpy(code + 19, &yaw, 4);
    code[23] = 0xC3;
    std::memcpy(at, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), at, sizeof(code));
}
using LookCall = void (*)(void* ps, std::uint32_t id);
DWORD WINAPI LookOtherThread(LPVOID arg) {
    auto* p = static_cast<std::pair<LookCall, void*>*>(arg);
    p->first(p->second, 1);
    return 0;
}
bool CallCaught(LookCall fn, void* ps) {
    __try { fn(ps, 1); return false; }
    __except (GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
}
void TestLookSource() {
    unsigned char stub[ls::detail::kStubSize]{};
    ls::detail::BuildStub(stub, 0x1122334455667788ull, 0x99AABBCCDDEEFF00ull);
    std::uint64_t t = 0, after = 0;
    std::memcpy(&t, stub + ls::detail::kStubTargetAt, 8);
    std::memcpy(&after, stub + ls::detail::kStubAfterAt, 8);
    Check(stub[0] == 0x53 && stub[1] == 0x56 && stub[2] == 0x48 && stub[5] == 0x28 && t == 0x1122334455667788ull &&
              after == 0x99AABBCCDDEEFF00ull && stub[0x2E] == 0xC3 && stub[0x2C] == 0x5E && stub[0x2D] == 0x5B,
          "stub: push rbx/rsi, sub rsp 0x28, the two absolute calls, pop, ret");
    unsigned char unwind[ls::detail::kUnwindSize]{};
    ls::detail::BuildUnwind(unwind);
    Check(unwind[0] == 1 && unwind[1] == 6 && unwind[2] == 3 && unwind[4] == 6 && unwind[5] == 0x42 && unwind[6] == 2 &&
              unwind[7] == 0x60 && unwind[8] == 1 && unwind[9] == 0x30, "unwind: prolog 6, alloc 0x28, push rsi, push rbx");
    Check(ls::detail::RetargetQword(pc::look_src_qword_orig) == pc::look_src_qword_new && ls::detail::RetargetQword(0) == 0 &&
              ((pc::look_src_qword_orig ^ pc::look_src_qword_new) & ~0x00FFFF0000000000ull) == 0,
          "retarget: only the rel32 low word changes");
    {
        LookImage img;
        ls::detail::ResetForTest();
        img.mem[pc::look_fn_store_rva + 3] ^= 1;
        g_look_log.clear();
        Check(std::strcmp(ls::Install(reinterpret_cast<void*>(img.base), &LookLog), "bytes") == 0 && !ls::Installed() &&
                  img.Qword() == pc::look_src_qword_orig && g_look_log.find("look_src=refused; reason=bytes") != std::string::npos,
              "a changed store tail refuses; the call is untouched");
    }
    {
        LookImage img;
        ls::detail::ResetForTest();
        img.mem[pc::look_pad_rva + 7] = 0x90;
        Check(std::strcmp(ls::Install(reinterpret_cast<void*>(img.base), &LookLog), "padding") == 0 && img.Qword() == pc::look_src_qword_orig,
              "a non-int3 padding byte refuses");
    }
    {
        LookImage img;
        ls::detail::ResetForTest();
        std::memcpy(img.mem + pc::look_src_qword_rva, &pc::look_src_qword_new, 8);
        Check(std::strcmp(ls::Install(reinterpret_cast<void*>(img.base), &LookLog), "bytes") == 0, "an already retargeted call refuses (bytes)");
    }
    {
        LookImage img;
        ls::detail::ResetForTest();
        g_look_log.clear();
        Check(ls::Install(reinterpret_cast<void*>(img.base), &LookLog) == nullptr && ls::Installed() &&
                  ls::PatchLive(reinterpret_cast<void*>(img.base)) && img.Qword() == pc::look_src_qword_new &&
                  img.mem[pc::look_pad_rva] == 0xE9 && g_look_log.find("look_src=installed; site=0xff29fc") != std::string::npos,
              "install: the call retargeted to the padding jump");
        std::int32_t rel = 0;
        std::memcpy(&rel, img.mem + pc::look_src_call_rva + 1, 4);
        Check(pc::look_src_next_ip + rel == pc::look_pad_rva, "the call now reaches the padding");
        WriteFakeLook(img.mem + pc::look_fn_rva);
        static FakePs ps{};
        ps.vt = img.base + pc::ps_vtable_alt_rva;
        ps.base_id = 0;
        ps.rec = reinterpret_cast<std::uintptr_t>(g_look_recs);
        ps.look = reinterpret_cast<std::uintptr_t>(g_look_pairs);
        const auto call = reinterpret_cast<LookCall>(img.base + pc::look_pad_rva);
        call(&ps, 1);
        Check(g_look_pairs[1][0] == 0.25f && g_look_pairs[1][1] == -1.5f, "nothing published: the game's (viewer) look stays");
        ls::Publish(reinterpret_cast<std::uintptr_t>(&ps), 1, 1, 0.1f, 2.0f);
        call(&ps, 1);
        Check(g_look_pairs[1][0] == 0.1f && g_look_pairs[1][1] == 2.0f, "published: the remote's look follows the game's write");
        call(&ps, 0);
        ls::Stats st{};
        ls::ReadStats(st);
        Check(st.installed && st.hits == 1 && st.refused == 1 && st.calls == 3, "another id is refused (counted), never written");
        g_look_recs[1][0] = 2;  // the game skipped the computation for id 1
        call(&ps, 1);
        Check(g_look_pairs[1][0] == 0.25f, "record bit 1: skipped, the game's value stays");
        g_look_recs[1][0] = 0;
        std::pair<LookCall, void*> arg{call, &ps};
        HANDLE th = CreateThread(nullptr, 0, &LookOtherThread, &arg, 0, nullptr);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        Check(g_look_pairs[1][0] == 0.25f, "another thread (block 0's worker) is never served");
        ps.vt = img.base + 0x10;
        call(&ps, 1);
        Check(g_look_pairs[1][0] == 0.25f, "a PS with another vtable is refused");
        ps.vt = img.base + pc::ps_vtable_alt_rva;
        ls::Withdraw();
        call(&ps, 1);
        Check(g_look_pairs[1][0] == 0.25f, "withdrawn: the game's value stays");
        ls::ReadStats(st);
        Check(st.skipped == 1 && st.refused == 2 && !st.faulted, "stats: skipped and refused");
        // An exception inside 0xFFAC00 unwinds through the stub to the caller's handler.
        img.mem[pc::look_fn_rva] = 0x0F;
        img.mem[pc::look_fn_rva + 1] = 0x0B;  // ud2
        FlushInstructionCache(GetCurrentProcess(), img.mem + pc::look_fn_rva, 2);
        Check(CallCaught(call, &ps), "the stub's unwind data carries an exception to the handler above it");
        ls::detail::ResetForTest();
    }
}

} // namespace

int main() {
    TestFireFilter();
    TestSeatShotHook();
    TestFireCensus();
    TestNoiseFilter();
    TestLookSource();
    TestStubBytes();
    TestRel32();
    TestBlockGate();
    TestPatchDword();
    TestKmSlots();
    TestPatchQword();
    TestRetargetCall();
    std::printf("%u partner input hook checks passed: stub bytes, rel32 reach/cache-line, dispatch-block gate "
                "(mismatch + retargeted rel32), locked dword CAS (success/refusal/split), key-manager slot map, "
                "data qword swap, fire filter (prologue/slot refusals, install, owner filter, stats, live check, census), fire/projectile census (ring, verdicts unchanged, forwarding projectile slots, refusals), noise filter, "
                "look source (stub/unwind bytes, refusals, install, publish/skip/thread/vtable/withdraw, unwind through the stub)\n", checks);
    return 0;
}
