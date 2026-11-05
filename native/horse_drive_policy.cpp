// Pure part of MGS_COOP_HORSE_DRIVE_V1 (red: stubs).
#include "mgscoop/horse_drive.hpp"
namespace mgscoop::horse_drive {
void Matrix(float, float, float, float, float (&m)[16]) noexcept { for (float& v : m) v = 0.f; }
void HorseUnderRider(const float (&body)[3], float, float (&out)[3]) noexcept { for (int i = 0; i < 3; ++i) out[i] = body[i]; }
bool Parse(const char*, std::size_t, Request& out) noexcept { out = Request{}; return false; }
const char* PlaceRefusal(const PlaceScope&) noexcept { return "off"; }
std::uint32_t Generation(std::uintptr_t, std::uintptr_t, std::uint32_t) noexcept { return 0; }
void NotePlaced(std::uint64_t) noexcept {}
bool PlacedFresh(std::uint64_t) noexcept { return false; }
void NoteRiderBody(const float (&)[3], std::uint64_t) noexcept {}
bool RiderBody(std::uint64_t, float (&)[3]) noexcept { return false; }
void Reset() noexcept {}
} // namespace mgscoop::horse_drive
