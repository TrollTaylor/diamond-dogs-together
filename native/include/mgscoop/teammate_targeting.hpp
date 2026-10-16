#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace mgscoop::teammate_targeting {
enum class Mode {observe, filter};
// Natural acquisition hook. Qualified owned-pair filters default on after
// install; worker A/B modes remain independent. No reticle drawing.
// Worker builds leave the vt+268 prompt queue to the private native pose
// window, whose own hook forwards through FilterPartnerPrompt.
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept,bool leave_vt268=false) noexcept;
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
using PromptFilter=bool (*)(void*,std::size_t,std::uint16_t,std::uint32_t,std::uint8_t,std::uintptr_t) noexcept;
inline std::atomic<PromptFilter> prompt_filter{nullptr};
}
inline void EndQuietJob() noexcept {detail::quiet_job={};}
// For a prompt queue hook owned elsewhere: true when the request was queued
// from the validated partner job and must not reach the local HUD.
inline bool FilterPartnerPrompt(void* ui,std::size_t field,std::uint16_t id,std::uint32_t priority,
    std::uint8_t enable,std::uintptr_t caller) noexcept {
    const auto filter=detail::prompt_filter.load(std::memory_order_acquire);
    return filter && filter(ui,field,id,priority,enable,caller);
}
void SetQuietMode(Mode mode) noexcept;
bool QuietStatusLine(char* out,std::size_t capacity) noexcept;
} // namespace mgscoop::teammate_targeting
