#pragma once
#ifdef _WIN32
#include <cstddef>
namespace mgscoop {
enum class PatchStatus { installed, invalid_argument, inaccessible, unexpected_target, protection_failed };
struct PatchResult {
    PatchStatus status;
    bool replacement_installed = false;
    bool protection_restored = true;
};
struct MappedImageSpan { void* base; std::size_t size; };
bool readable_image_range(const void* context, std::size_t offset, std::size_t count) noexcept;
// Only changes one aligned data pointer. Caller must pin the replacement's module
// and publish its original function before entry. No executable bytes are changed.
PatchResult exchange_import(void* module, std::size_t span, std::size_t offset,
                            void* expected, void* replacement) noexcept;
}
#endif
