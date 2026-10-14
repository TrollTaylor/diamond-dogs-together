#pragma once
// Distinct local-only unique two-entry MGO rest reconstruction pins.
// Normal/release catalogs are unchanged. No game asset bytes in this header.
// Admission of this archive is not proof its resident bank was selected.
#include <cstddef>
#include <string_view>
namespace mgscoop::mgo_unique_rest {
inline constexpr char profile[] = "mgo_unique_rest_v1";
inline constexpr char archive_sha[] = "92e935bced30dcd7a1eda498462a375a8594241a1cfa7228026e3e346e886c25";
inline constexpr char catalog_sha[] = "cd23aa3146823af8577079009ed54cfa6f1508f2ae2e7d55e8a0b55dab46ad51";
inline constexpr std::size_t archive_bytes = 16360448;
inline constexpr unsigned motions = 21, total = 0;
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER)
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
        p.profile != L"mgo_unique_rest_v1" || p.bytes != L"16360448" || p.motions != L"21" || p.capacity != L"0" ||
        p.lab_mode != L"lab" || p.build_profile != L"mgo-unique-rest" || p.native_stage != L"gun" ||
        p.digest.size() != 64 || p.catalog.size() != 64 || p.game.empty() || p.game.size() != p.expected_game.size()) return false;
    for (const auto c : p.run) if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    const auto fold = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c; };
    for (std::size_t i = 0; i < 64; ++i)
        if (fold(p.digest[i]) != archive_sha[i] || fold(p.catalog[i]) != catalog_sha[i]) return false;
    for (std::size_t i = 0; i < p.game.size(); ++i) if (fold(p.game[i]) != fold(p.expected_game[i])) return false;
    return true;
}
} // namespace mgscoop::mgo_unique_rest
