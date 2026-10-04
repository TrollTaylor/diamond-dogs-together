// Retry path of PrepareSessionAllocation (synthesis gap G11): after the first
// request installs the allocation bridge's MinHook detour on BlockHeapAlloc, a
// second request must reuse it. This fixture installs the real bridge on an
// executable copy of the verified BlockHeapAlloc body (never executed) and
// checks the exact byte layout the reuse gate relies on. No game access.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/allocation_bridge.hpp"
#include "mgscoop/session_allocation_reuse.hpp"
#include "session_allocation_contract.hpp"

namespace {
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
void* Heap(std::uint64_t, std::uint64_t, std::uint32_t) { return nullptr; }
} // namespace

int main() {
    using namespace mgscoop;
    namespace bridge = mgscoop::allocation_bridge;
    constexpr std::size_t count = sizeof(block_allocator_bytes);
    static_assert(count == 95, "verified BlockHeapAlloc body length");

    // Decision table.
    Check(DecideAllocationPrepare(0, 0x140000000) == AllocationPrepare::install, "nothing installed -> install");
    Check(DecideAllocationPrepare(0x140000000, 0x140000000) == AllocationPrepare::reuse, "same base -> reuse");
    Check(DecideAllocationPrepare(0x140000000, 0x150000000) == AllocationPrepare::refuse, "other base -> refuse");

    // The verified body starts with exactly one five-byte instruction.
    const unsigned char first[] = {0x48, 0x89, 0x5c, 0x24, 0x08};
    Check(std::memcmp(block_allocator_bytes, first, sizeof(first)) == 0, "first instruction is mov [rsp+8],rbx");

    // Synthetic layouts.
    unsigned char live[count]{};
    std::memcpy(live, block_allocator_bytes, count);
    Check(!DetouredEntryMatches(live, block_allocator_bytes, count), "unhooked entry is not a detour");
    live[0] = 0xE9; live[1] = 0x11; live[2] = 0x22; live[3] = 0x33; live[4] = 0x44;
    Check(DetouredEntryMatches(live, block_allocator_bytes, count), "jmp rel32 over the first instruction matches");
    live[5] ^= 0xff;
    Check(!DetouredEntryMatches(live, block_allocator_bytes, count), "a changed tail byte refuses");
    live[5] ^= 0xff;
    live[count - 1] ^= 0x01;
    Check(!DetouredEntryMatches(live, block_allocator_bytes, count), "a changed last byte refuses");
    live[count - 1] ^= 0x01;
    live[0] = 0xEB;
    Check(!DetouredEntryMatches(live, block_allocator_bytes, count), "a short jump refuses");
    Check(!DetouredEntryMatches(nullptr, block_allocator_bytes, count), "null live refuses");
    Check(!DetouredEntryMatches(live, block_allocator_bytes, kDetourPatchBytes), "too short refuses");

    // Real MinHook detour through the allocation bridge on an executable copy.
    auto* page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Check(page != nullptr, "executable fixture page");
    std::memset(page, 0xCC, 4096);
    std::memcpy(page, block_allocator_bytes, count);
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    const auto target = reinterpret_cast<bridge::Allocate>(page);
    const bridge::Plan plan{&Heap, {{0x1000, 88, 8, 115}, {0x2000, 144, 8, 115}}};

    // First request: install over the unhooked, verified bytes.
    Check(bridge::Install(target, block_allocator_bytes, count, plan) == bridge::InstallResult::installed, "first install");
    Check(std::memcmp(page, block_allocator_bytes, count) != 0, "installed detour changes the verified body (hash would fail)");
    Check(DetouredEntryMatches(page, block_allocator_bytes, count), "MinHook leaves exactly the verified detour layout");

    // Second request (retry after reset): reuse, never a second install.
    const std::uintptr_t installed_base = 0x140000000;
    Check(DecideAllocationPrepare(installed_base, 0x140000000) == AllocationPrepare::reuse, "retry decides reuse");
    Check(bridge::Install(target, block_allocator_bytes, count, plan) == bridge::InstallResult::already_installed,
          "retry sees the adapter's install as already_installed");
    Check(DetouredEntryMatches(page, block_allocator_bytes, count), "retry leaves the detour unchanged");
    {
        bridge::Scope scope;
        Check(scope.acquired(), "retry can still acquire an allocation scope");
    }
    // The bridge does not distinguish plans, which is why the adapter keys
    // reuse on its own record of the base it installed for.
    const bridge::Plan other{&Heap, {{0x3000, 88, 8, 115}, {0x4000, 144, 8, 115}}};
    Check(bridge::Install(target, block_allocator_bytes, count, other) == bridge::InstallResult::already_installed,
          "a different plan is also already_installed (adapter must gate on its base)");
    Check(DecideAllocationPrepare(installed_base, 0x150000000) == AllocationPrepare::refuse, "different base refused by the adapter");

    // Removal restores the verified body; the reuse gate then refuses.
    Check(bridge::RemoveAfterCallersStopped(), "fixture removal");
    Check(std::memcmp(page, block_allocator_bytes, count) == 0, "removal restores the verified body");
    Check(!DetouredEntryMatches(page, block_allocator_bytes, count), "restored body is not a detour");
    VirtualFree(page, 0, MEM_RELEASE);

    std::printf("%u session-allocation reuse checks: decision table, detour layout, real MinHook install/reuse/"
                "already_installed/removal on the verified BlockHeapAlloc bytes (never executed); no game calls\n", checks);
    return 0;
}
