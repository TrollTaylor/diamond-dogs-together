#pragma once
// Local-only MGO rest reconstruction pins. No game bytes in this header.
// Admission of this archive is not proof its resident bank was selected.
#include <cstddef>
#include <string_view>
namespace mgscoop::mgo_sleep_rest {
inline constexpr char profile[] = "mgo_sleep_rest_v1";
inline constexpr char archive_sha[] = "0a8a13467b6782ee3e32a5b99f8c950cea9a6d8cb486faac10c0a417200bae54";
inline constexpr char catalog_sha[] = "d70047206d28561d2ce8831ba0ab0311c8cb2cdb864a01373907dd4318e3f755";
inline constexpr std::size_t archive_bytes = 26949632;
inline constexpr unsigned motions = 21, total = 0;
#if defined(MGS_BACKGROUND_WORKER)
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
        p.profile != L"mgo_sleep_rest_v1" || p.bytes != L"26949632" || p.motions != L"21" || p.capacity != L"0" ||
        p.lab_mode != L"lab" || p.build_profile != L"research" || p.native_stage != L"gun" ||
        p.digest.size() != 64 || p.catalog.size() != 64 || p.game.empty() || p.game.size() != p.expected_game.size()) return false;
    for (const auto c : p.run) if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    const auto fold = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c; };
    for (std::size_t i = 0; i < 64; ++i)
        if (fold(p.digest[i]) != archive_sha[i] || fold(p.catalog[i]) != catalog_sha[i]) return false;
    for (std::size_t i = 0; i < p.game.size(); ++i) if (fold(p.game[i]) != fold(p.expected_game[i])) return false;
    return true;
}
} // namespace mgscoop::mgo_sleep_rest
