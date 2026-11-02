// Original MGS Co-op: the soldier perception filter patch (docs/world-sync-design.md 7.13; header soldier_perceive.hpp).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mgscoop/soldier_perceive.hpp"
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <strsafe.h>
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
#endif

} // namespace mgscoop::soldier_perceive
