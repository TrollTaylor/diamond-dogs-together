// MGS_COOP_HORSE_DRIVE_V1 live write (red: stub).
#include "mgscoop/horse_drive.hpp"
namespace mgscoop::horse_drive {
const char* PinsReason(std::uintptr_t, std::size_t) noexcept { return "pins"; }
const char* Write(std::uintptr_t, std::size_t, std::uintptr_t, std::uintptr_t, std::uint16_t, float, float, float, float) noexcept { return "off"; }
} // namespace mgscoop::horse_drive
