#pragma once
// Distinct local-only separate four-motion standing-cycle reconstruction pins.
// Normal/release catalogs are unchanged. No game asset bytes in this header.
// Admission of this archive is not proof its resident bank was selected.
#include <cstddef>
#include <string_view>
namespace mgscoop::mgo_standing_cycle {
inline constexpr char profile[] = "mgo_standing_cycle_v1";
inline constexpr char archive_sha[] = "882e683b296e97f355b2d2e888194d9ee2bea3c64ee1ea93ba1654601e1c34c2";
inline constexpr char catalog_sha[] = "88a1f5c70e2a0812429e2b75bb6ed55718e0aafd9b53cbe16aa0464b4f83bac4";
inline constexpr std::size_t archive_bytes = 16362496;
inline constexpr unsigned motions = 21, total = 0;
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER) && !defined(MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER)
inline constexpr bool compiled = true;
#else
inline constexpr bool compiled = false;
#endif
inline bool Compiled() noexcept { return compiled; }
struct ContextPins {
    std::wstring_view run, role, desktop, game, expected_game;
    std::wstring_view profile, digest, catalog, bytes, motions, capacity;
    std::wstring_view lab_mode, build_profile, native_stage;
};
inline bool ContextMatches(const ContextPins& p) noexcept {
    if (!Compiled() || p.run.size() != 32 || (p.role != L"host" && p.role != L"guest") ||
        p.desktop.size() != 42 || p.desktop.substr(0, 10) != L"MGSWorker_" || p.desktop.substr(10) != p.run ||
        p.profile != L"mgo_standing_cycle_v1" || p.bytes != L"16362496" || p.motions != L"21" || p.capacity != L"0" ||
        p.lab_mode != L"lab" || p.build_profile != L"mgo-standing-cycle" || p.native_stage != L"gun" ||
        p.digest.size() != 64 || p.catalog.size() != 64 || p.game.empty() || p.game.size() != p.expected_game.size()) return false;
    for (const auto c : p.run) if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    const auto fold = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c; };
    for (std::size_t i = 0; i < 64; ++i)
        if (fold(p.digest[i]) != archive_sha[i] || fold(p.catalog[i]) != catalog_sha[i]) return false;
    for (std::size_t i = 0; i < p.game.size(); ++i) if (fold(p.game[i]) != fold(p.expected_game[i])) return false;
    return true;
}
} // namespace mgscoop::mgo_standing_cycle
