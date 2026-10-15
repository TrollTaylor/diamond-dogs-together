// Synthetic-image stock guard fixture. Executes only our emitted stub and
// two tiny fixture return thunks; never executes MGSV code or starts a game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_support_guard.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace g = mgscoop::partner_support_guard;
namespace c = mgscoop::partner_player_contract;
unsigned checks = 0;
std::string logged;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %u: %s\n%s", checks, message, logged.c_str()); std::exit(1); }
}
void Log(const char* line) noexcept { try { logged += line; } catch (...) {} }
template<class T> void Put(unsigned char* at, T value) noexcept { std::memcpy(at, &value, sizeof(value)); }
template<class T> T Get(const unsigned char* at) noexcept { T v{}; std::memcpy(&v, at, sizeof(v)); return v; }
constexpr std::size_t kSpan = 0x108D000;
unsigned char* Image() {
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, kSpan, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "synthetic image allocation");
    for (const auto& site : c::code_sites)
        if (site.rva <= kSpan && site.size <= kSpan - site.rva) std::memcpy(image + site.rva, site.bytes, site.size);
    return image;
}
void TestPatch() {
    auto* image = Image();
    g::detail::ResetForTest();
    Check(g::Install(image, kSpan, false, Log) != nullptr, "fingerprint refusal");
    Check(Get<std::uint32_t>(image + c::support_stock_load_rva) == c::support_stock_load_orig, "fingerprint refusal unchanged");
    g::detail::ResetForTest();
    Check(g::Install(image, c::support_stock_load_rva, true, Log) != nullptr, "short image refused");
    g::detail::ResetForTest();
    image[c::support_stock_load_rva + 6] ^= 1;
    Check(g::Install(image, kSpan, true, Log) != nullptr, "full seven-byte load mismatch refused");
    Check(Get<std::uint32_t>(image + c::support_stock_load_rva) == c::support_stock_load_orig, "mismatch no publish");
    image[c::support_stock_load_rva + 6] ^= 1;
    g::detail::ResetForTest();
    image[c::support_stock_pad_end_rva - 1] = 0;
    Check(g::Install(image, kSpan, true, Log) != nullptr, "last unused padding byte mismatch refused");
    image[c::support_stock_pad_end_rva - 1] = 0xCC;
    g::detail::ResetForTest();
    Check(g::Install(image, kSpan, true, Log) == nullptr && g::Installed() && g::Live(), "permanent synthetic install");
    Check(Get<std::uint32_t>(image + c::support_stock_load_rva) == c::support_stock_load_new, "one published dword");
    Check(image[c::support_stock_load_rva + 4] == 2 && image[c::support_stock_load_rva + 5] == 0 &&
          image[c::support_stock_load_rva + 6] == 0, "unreachable original tail preserved");
    const auto rel8 = static_cast<std::int8_t>(image[c::support_stock_load_rva + 1]);
    Check(static_cast<std::intptr_t>(c::support_stock_load_rva + 2) + rel8 ==
          static_cast<std::intptr_t>(c::support_stock_pad_rva), "EB A1 reaches verified padding");
    Check(!mgscoop::near_code::CrossesCacheLine(reinterpret_cast<std::uintptr_t>(image) + c::support_stock_load_rva, 4),
          "dword stays in one cache line");
    Check(!g::Arm(0x1234, 0) && !g::Armed(0x1234), "instance zero can never arm");
    Check(g::Arm(0x1234) && g::Armed(0x1234), "key authorizes only partner P");
    Check(!g::Enter(0x5678) && !g::Enter(0x1234, 0), "wrong P/id window refused");
    Check(g::Enter(0x1234), "native drain window opens");
    g::Stats stats{};
    g::ReadStats(stats);
    Check(stats.armed && stats.in_window && stats.windows == 1, "armed window telemetry");
    bool entered = true;
    std::thread foreign([&] { entered = g::Enter(0x1234); g::Leave(); });
    foreign.join();
    Check(!entered, "foreign/nested drain denied");
    g::ReadStats(stats);
    Check(stats.in_window, "foreign Leave cannot close hook thread window");
    g::Leave();
    g::ReadStats(stats);
    Check(!stats.in_window && stats.armed, "Leave closes only window");
    g::Disarm();
    Check(!g::Armed(0x1234) && !g::Enter(0x1234), "key off denies native window");
    Check(g::Live() && g::Install(image, kSpan, true, Log) == nullptr, "Disarm never removes patch");
    image[c::support_stock_load_rva + 4] ^= 1;
    Check(!g::Live() && !g::Arm(0x1234), "live check includes untouched instruction tail");
    image[c::support_stock_load_rva + 4] ^= 1;
    g::detail::ResetForTest();
    VirtualFree(image, 0, MEM_RELEASE);
}
void TestStub() {
    // Exact state offsets consumed by the emitted machine code; this private
    // fixture state is independent of the installed guard's state.
    struct alignas(8) State { LONG id, hits, tid, refused; LONG64 p; LONG key; };
    static_assert(offsetof(State, p) == 0x10 && offsetof(State, key) == 0x18, "fixture state ABI");
    unsigned char player[0x208]{}, other[0x208]{}, rec[0x280]{};
    constexpr std::uint32_t original = 0x12345678u, forced = 0x6A5B4C3Du;
    Put(player + 0x204, original); Put(other + 0x204, original);
    State state{1, 0, static_cast<LONG>(GetCurrentThreadId()), 0,
                static_cast<LONG64>(reinterpret_cast<std::uintptr_t>(player)), 1};
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(memory != nullptr, "standalone stub allocation");
    // Back is ret (the original load's eax); remote is mov eax,sentinel; ret.
    memory[0x100] = 0xC3;
    memory[0x110] = 0xB8; Put(memory + 0x111, forced); memory[0x115] = 0xC3;
    unsigned char code[g::detail::kStubSize]{};
    g::detail::BuildStub(code, reinterpret_cast<std::uintptr_t>(&state),
                        reinterpret_cast<std::uintptr_t>(memory + 0x100),
                        reinterpret_cast<std::uintptr_t>(memory + 0x110));
    Check(Get<std::uintptr_t>(code + g::detail::kStubStateAt) == reinterpret_cast<std::uintptr_t>(&state),
          "stub state pointer position");
    Check(std::memcmp(code, "\x41\x8b\x81\x04\x02\x00\x00", 7) == 0, "stub replays complete original load first");
    std::memcpy(memory, code, sizeof(code));
    DWORD previous = 0;
    Check(VirtualProtect(memory, 0x1000, PAGE_EXECUTE_READ, &previous) != 0, "stub execute protection");
    FlushInstructionCache(GetCurrentProcess(), memory, 0x1000);
    using StubFn = std::uint32_t (*)(void*, const void*, std::uint32_t, const void*);
    const auto stub = reinterpret_cast<StubFn>(memory);
    for (unsigned cls = 0; cls < 8; ++cls) {
        Put(rec + 0x27C, (cls << 6) | 0x800010u);
        Check(stub(nullptr, rec, 1, player) == ((cls == 2 || cls == 3) ? forced : original), "real stub support-class matrix");
    }
    Put(rec + 0x27C, 2u << 6);
    Check(stub(nullptr, rec, 0, player) == original && stub(nullptr, rec, 2, player) == original, "real stub player id matrix");
    Check(stub(nullptr, rec, 0x401, player) == original, "real stub full soldier id preserved");
    Check(stub(nullptr, rec, 1, other) == original, "real stub requires armed P");
    state.tid ^= 1;
    Check(stub(nullptr, rec, 1, player) == original, "real stub TEB thread refusal");
    state.tid ^= 1; state.id = -1;
    Check(stub(nullptr, rec, 1, player) == original, "real stub outside drain");
    state.id = 1; state.key = 0;
    Check(stub(nullptr, rec, 1, player) == original, "real stub independent key off");
    state.key = 1;
    std::uint32_t foreign_value = forced;
    std::thread foreign([&] { foreign_value = stub(nullptr, rec, 1, player); });
    foreign.join();
    Check(foreign_value == original, "real stub another OS thread refuses");
    Check(state.hits == 2 && state.refused >= 11, "real stub hit and refusal counters");
    VirtualFree(memory, 0, MEM_RELEASE);
}
} // namespace
int main() {
    TestPatch(); TestStub();
    std::printf("partner_support_guard_fixture: %u checks; synthetic code only, no game\n", checks);
    return 0;
}
