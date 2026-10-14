#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::qualified_sleep_lookup_runtime {
using Log=void(*)(const char*) noexcept;
struct Authorization {
 bool exactRetailPe=false,offline=false,copiedSaveLab=false;
 bool(*privateWorkerCurrent)() noexcept=nullptr;
 bool(*profileCurrent)(const wchar_t*) noexcept=nullptr;
 bool(*readOriginal)(std::uintptr_t,void*,std::size_t) noexcept=nullptr;
};
// Default OFF. Initial hooks ONLY forward the actual native originals and
// observe bounded diagnostics. No native resource methods or clones execute.
bool Prepare(std::uintptr_t,std::size_t,const wchar_t*,const Authorization&,Log) noexcept;
bool Prepared() noexcept;
bool Status(char*,std::size_t) noexcept;
// Overlay ONLY actually captured/live owned detour heads after the caller has
// read the existing composed original view. It performs no first base read.
bool OverlayOriginal(std::uintptr_t,void*,std::size_t) noexcept;
}
