// Original MGS Co-op round 3 direct-motion comparator patch.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/partner_dm_patch.hpp"
#include "mgscoop/near_code.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_dm_patch {
namespace {
namespace c = mgscoop::partner_player_contract;
volatile LONG g_patched = 0;

void Emit(LogFn log, const char* line) noexcept { if (log) log(line); }

// The 28-byte block at 0x1198E12 (contract site) equals the image contract.
bool BlockOk(const void* base) noexcept {
    static constexpr unsigned char expected[] = {0x8b, 0x81, 0x04, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x49, 0x48, 0x48, 0x8d, 0x53,
                                                 0x28, 0xc1, 0xe8, 0x08, 0xa8, 0x01, 0x44, 0x0f, 0x44, 0xc7, 0xe8, 0xd2, 0xfc,
                                                 0xff, 0xff};
    unsigned char live[sizeof(expected)]{};
    std::memcpy(live, reinterpret_cast<const unsigned char*>(base) + c::dm_patch_block_rva, sizeof(live));
    // Offset 17..20 (44 0f 44 c7) is the patched dword; ignore it so a patched
    // block still validates (idempotent revert).
    for (std::size_t i = 0; i < sizeof(expected); ++i) {
        if (i >= 17 && i < 21) continue;
        if (live[i] != expected[i]) return false;
    }
    return true;
}
} // namespace

bool Patch(const void* base, LogFn log) noexcept {
    if (!base) return false;
    if (g_patched) return true;
    if (!BlockOk(base)) { Emit(log, "partner_dm=refused; reason=comparator_bytes\r\n"); return false; }
    void* site = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + c::dm_patch_rva;
    if (const char* why = near_code::PatchDword(site, c::dm_patch_old, c::dm_patch_new)) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_dm=refused; reason=patch_%s\r\n", why))) Emit(log, line);
        return false;
    }
    InterlockedExchange(&g_patched, 1);
    Emit(log, "partner_dm=patched; rva=0x1198e25; old=440f44c7; new=0f1f4000\r\n");
    return true;
}

void Revert(const void* base, LogFn log, const char* reason) noexcept {
    if (!base || !g_patched) return;
    void* site = reinterpret_cast<unsigned char*>(const_cast<void*>(base)) + c::dm_patch_rva;
    if (const char* why = near_code::PatchDword(site, c::dm_patch_new, c::dm_patch_old)) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_dm=refused; reason=revert_%s\r\n", why))) Emit(log, line);
        return;
    }
    InterlockedExchange(&g_patched, 0);
    char line[96]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_dm=reverted; reason=%s\r\n", reason ? reason : "unknown")))
        Emit(log, line);
}

bool Patched() noexcept { return g_patched != 0; }

} // namespace mgscoop::partner_dm_patch
