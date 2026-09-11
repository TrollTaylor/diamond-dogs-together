#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "mgscoop/win_import_patch.hpp"
#include <cstdint>
namespace mgscoop {
bool readable_image_range(const void* context, std::size_t offset, std::size_t count) noexcept {
    if (!context) return false;
    const auto& image = *static_cast<const MappedImageSpan*>(context);
    if (!image.base || offset > image.size || count > image.size - offset) return false;
    auto address = reinterpret_cast<std::uintptr_t>(image.base) + offset;
    const auto end = address + count;
    if (end < address) return false;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) || region.AllocationBase != image.base ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto access = region.Protect & 0xff;
        if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY &&
            access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address) return false;
        address = next < end ? next : end;
    }
    return true;
}
PatchResult exchange_import(void* module, std::size_t span, std::size_t offset, void* expected, void* replacement) noexcept {
    if (!module || !expected || !replacement || expected == replacement || offset > span || sizeof(void*) > span - offset) return {PatchStatus::invalid_argument};
    auto* slot = reinterpret_cast<void* volatile*>(static_cast<unsigned char*>(module) + offset);
    if ((reinterpret_cast<std::uintptr_t>(slot) % alignof(void*)) != 0) return {PatchStatus::invalid_argument};
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(const_cast<void**>(slot), &region, sizeof(region)) || region.State != MEM_COMMIT ||
        region.AllocationBase != module || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        reinterpret_cast<std::uintptr_t>(slot) + sizeof(void*) > reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
        return {PatchStatus::inaccessible};
    const DWORD access = region.Protect & 0xff;
    // This helper is for data imports only; do not remove execution permission
    // from a page that other threads could be executing.
    if (access != PAGE_READONLY && access != PAGE_READWRITE && access != PAGE_WRITECOPY)
        return {PatchStatus::inaccessible};
    if (*slot != expected) return {PatchStatus::unexpected_target};
    DWORD previous = 0;
    if (!VirtualProtect(const_cast<void**>(slot), sizeof(void*), PAGE_READWRITE, &previous)) return {PatchStatus::protection_failed};
    const auto original = InterlockedCompareExchangePointer(slot, replacement, expected);
    const bool installed = original == expected;
    DWORD ignored = 0;
    if (VirtualProtect(const_cast<void**>(slot), sizeof(void*), previous, &ignored))
        return {installed ? PatchStatus::installed : PatchStatus::unexpected_target, installed, true};
    // Restore publication first if protection restoration fails. The wrapper stays
    // pinned even if a third party raced this slot; never claim an unsafe unload.
    if (installed) InterlockedCompareExchangePointer(slot, expected, replacement);
    const bool restored = VirtualProtect(const_cast<void**>(slot), sizeof(void*), previous, &ignored) != FALSE;
    const bool still_installed = *slot == replacement;
    return {PatchStatus::protection_failed, still_installed, restored};
}
}
