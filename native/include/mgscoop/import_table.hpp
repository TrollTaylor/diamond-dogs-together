#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop {
enum class ImportStatus { found, not_found, invalid_image, ambiguous, invalid_argument };
struct ImportSlot { ImportStatus status; std::size_t offset = 0; };
using ReadableRange = bool(*)(const void* context, std::size_t offset, std::size_t count) noexcept;
// Input is a mapped PE image (RVAs are indexes), with an independently known span.
// This parser performs no writes and does not load modules or resolve exports.
ImportSlot find_import_slot(const std::uint8_t* image, std::size_t span,
                            const char* dll, const char* symbol,
                            ReadableRange readable = nullptr, const void* context = nullptr) noexcept;
}
