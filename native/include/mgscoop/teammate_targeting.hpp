#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::teammate_targeting {
enum class Mode {observe, filter};
// Natural acquisition hook. Qualified owned-pair filters default on after
// install; worker A/B modes remain independent. No reticle drawing.
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
void SetEnabled(bool enabled) noexcept;
// Selecting either mode starts a fresh capped observation budget for live A/B.
void SetMode(Mode mode) noexcept;
bool StatusLine(char* out,std::size_t capacity) noexcept;
// Independent native reticle classification mode; preserves original geometry.
// Filter defaults on after an owned hit/category/green-color private receipt.
void SetReticleMode(Mode mode) noexcept;
bool ReticleStatusLine(char* out,std::size_t capacity) noexcept;
// Called only by the existing before/after callbacks of the validated partner
// instance-1 job. Scope is thread-local and ends before the after-job UI reads.
void BeginQuietJob(std::uintptr_t player,std::uintptr_t ui,std::uint32_t owner_thread) noexcept;
namespace detail {
// Pure shared TLS state: the input library can always disarm its job without
// introducing a dependency on native targeting hooks or their UI libraries.
struct QuietJob {std::uintptr_t p{},ui{};std::uint32_t owner_thread{};bool armed{};};
inline thread_local QuietJob quiet_job{};
}
inline void EndQuietJob() noexcept {detail::quiet_job={};}
void SetQuietMode(Mode mode) noexcept;
bool QuietStatusLine(char* out,std::size_t capacity) noexcept;
} // namespace mgscoop::teammate_targeting
