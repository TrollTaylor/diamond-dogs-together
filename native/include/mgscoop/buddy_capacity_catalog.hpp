#pragma once
// Original local-only catalog; game-derived experiment stays outside packages.
// No catalog value is accepted from the caller. Baseline21 pins remain separate.
#include <cstddef>
#include <cstdint>
namespace mgscoop::buddy_capacity {
inline constexpr char profile[] = "buddy_capacity_two_v1";
// Scratch archive mgs-coop-buddy-capacity-variants-v2.dat. Replaces the
// variant-00-only file. SHA-256 and size were rechecked on that file.
// catalog.json beside it hashes to catalog_sha. Game bytes stay outside git.
inline constexpr char archive_sha[] = "4b290cddc14f1146dce5a64a1008365d572b424b319874ef77e9496634d75ec9";
inline constexpr char catalog_sha[] = "4a7e7ea890adf8f261368cd546ccfd3dbfdc4407ef6af44aff308b98e53d0989";
inline constexpr std::size_t archive_bytes = 17501184;
// The earlier horse-00-only QAR. Mount refuses this size instead of treating it as capacity.
inline constexpr std::size_t superseded_archive_bytes = 16659456;
inline constexpr unsigned motions = 21, total = 2;
// FPKDs whose FOX2 totalCount/realizedCount were authored to 2 with one extra
// locator. Each added horse pack changes only that FOX2; other entries match
// the retail pack. The selector's retained hash is the companion .fpk, not
// this fpkd. The walker selector qword is buddy_wg2_00.fpk; 0xddd00 rewrites
// that key to buddy_wg2_00.fpkd. buddy_walker2_00.fpk does not match the qword.
inline constexpr char horse_variant_packages[][56] = {
    "/Assets/tpp/pack/buddy/horse/buddy_horse2_00.fpkd",
    "/Assets/tpp/pack/buddy/horse/buddy_horse2_03.fpkd",
    "/Assets/tpp/pack/buddy/horse/buddy_horse2_02_0.fpkd",
    "/Assets/tpp/pack/buddy/horse/buddy_horse2_02_1.fpkd",
    "/Assets/tpp/pack/buddy/horse/buddy_horse2_02_2.fpkd",
    "/Assets/tpp/pack/buddy/horse/buddy_horse2_05.fpkd",
    "/Assets/tpp/pack/buddy/horse/buddy_horse2_04.fpkd",
};
static_assert(sizeof(horse_variant_packages) / sizeof(horse_variant_packages[0]) == 7, "seven selector horse variants");
inline constexpr const char* horse_package = horse_variant_packages[0];
inline constexpr char walker_package[] = "/Assets/tpp/pack/buddy/walkergear/buddy_wg2_00.fpkd";
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_BUDDY_CAPACITY_TEST)
inline constexpr bool compiled = true;
#else
inline constexpr bool compiled = false;
#endif
// Retail mgsvtpp.exe 085c2f82... selects the main buddy FPK in A46870 and
// copies that u64 into the current controller+0x38 through the verified
// wrapper at 0x857a0 (lea rcx,[controller+0x38] at 0xa4428b). These are the
// exact table qwords at 0x2a74960 and the walker qword at 0x2a74a88.
// A match identifies the requested .fpk. Binder 0xddd00 rewrites the same
// path to .fpkd. That does not prove two bodies loaded, and it does not
// authorize a mount.
struct MainPack { std::uint64_t hash; const char* token; };
inline constexpr MainPack kHorseMainPacks[] = {
    {0x522b4a43c0a13f24ull, "00"},
    {0x5229639b03a815bbull, "03"},
    {0x5229a63819adce7aull, "02_0"},
    {0x522b2c9cca41c514ull, "02_1"},
    {0x522a0e2f0f5f56baull, "02_2"},
    {0x5229b692bf79e39eull, "05"},
    {0x522974ae3a6305fcull, "04"},
};
inline constexpr MainPack kWalkerMainPack{0x5229f0178ecad0fcull, "00"};
inline const char* MatchHorseMainPack(std::uint64_t hash) noexcept {
    for (const auto& row : kHorseMainPacks) if (row.hash == hash) return row.token;
    return nullptr;
}
inline const char* MatchWalkerMainPack(std::uint64_t hash) noexcept {
    return hash == kWalkerMainPack.hash ? kWalkerMainPack.token : nullptr;
}
} // namespace mgscoop::buddy_capacity
