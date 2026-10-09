// Original partner name tag shared state (docs/partner-nametag.md). Copies only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include "mgscoop/nametag.hpp"

namespace mgscoop::nametag {
namespace {
// Constant-initialized state only (no destructors at unload).
SRWLOCK lock = SRWLOCK_INIT;
std::atomic<bool> allowed{false}, host{false}, refused{false}, shutting_down{false};
std::atomic<bool> setting_on{true};
std::atomic<std::uint8_t> setting_crosshair{static_cast<std::uint8_t>(Crosshair::aim)};
std::atomic<std::uint32_t> setting_max_m{300};
Frame frame{};
bool have_frame{};
RenderStats stats{};
Telemetry telemetry{};
} // namespace

void ResetState() noexcept {
    AcquireSRWLockExclusive(&lock);
    frame = {}; have_frame = false; stats = {}; telemetry = {};
    ReleaseSRWLockExclusive(&lock);
}
void Configure(bool value, bool is_host, const Settings& settings) noexcept {
    ResetState();
    refused.store(false, std::memory_order_release);
    shutting_down.store(false, std::memory_order_release);
    host.store(is_host, std::memory_order_release);
    setting_on.store(settings.on, std::memory_order_release);
    setting_crosshair.store(static_cast<std::uint8_t>(settings.crosshair), std::memory_order_release);
    setting_max_m.store(settings.max_m, std::memory_order_release);
    allowed.store(value, std::memory_order_release);
}
bool Enabled() noexcept { return allowed.load(std::memory_order_acquire) && !refused.load(std::memory_order_acquire); }
bool Host() noexcept { return host.load(std::memory_order_acquire); }
void Refuse() noexcept {
    refused.store(true, std::memory_order_release);
    AcquireSRWLockExclusive(&lock);
    have_frame = false;
    ReleaseSRWLockExclusive(&lock);
}
Settings GetSettings() noexcept {
    Settings s{};
    s.on = setting_on.load(std::memory_order_acquire);
    s.crosshair = static_cast<Crosshair>(setting_crosshair.load(std::memory_order_acquire));
    s.max_m = setting_max_m.load(std::memory_order_acquire);
    return s;
}
void SetOn(bool value) noexcept { setting_on.store(value, std::memory_order_release); }
void SetCrosshair(Crosshair value) noexcept { setting_crosshair.store(static_cast<std::uint8_t>(value), std::memory_order_release); }

void Publish(const Frame& value) noexcept {
    if (!Enabled() || shutting_down.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&lock);
    frame = value; have_frame = true;
    ReleaseSRWLockExclusive(&lock);
}
bool Take(Frame& out) noexcept {
    if (!Enabled() || shutting_down.load(std::memory_order_acquire)) return false;
    AcquireSRWLockShared(&lock);
    const bool ok = have_frame;
    if (ok) out = frame;
    ReleaseSRWLockShared(&lock);
    return ok;
}
bool RenderPending(std::uint64_t now_ms) noexcept {
    Frame f{};
    return Take(f) && (f.visible || f.crosshair) && now_ms >= f.tick_ms && now_ms - f.tick_ms <= kStaleMs;
}
void Shutdown() noexcept {
    shutting_down.store(true, std::memory_order_release);
}
bool ShuttingDown() noexcept { return shutting_down.load(std::memory_order_acquire); }

void NoteRender(const RenderNote& note) noexcept {
    AcquireSRWLockExclusive(&lock);
    if (stats.frames != UINT32_MAX) ++stats.frames;
    stats.present_thread = note.thread;
    stats.bb[0] = note.bb[0]; stats.bb[1] = note.bb[1];
    stats.display[0] = note.display[0]; stats.display[1] = note.display[1];
    stats.age_ms = note.age_ms;
    stats.crosshair = note.crosshair;
    switch (note.kind) {
    case Drawn::stale: if (stats.stale_frames != UINT32_MAX) ++stats.stale_frames; break;
    case Drawn::hidden: if (stats.hidden_frames != UINT32_MAX) ++stats.hidden_frames; break;
    case Drawn::drawn:
        if (stats.drawn != UINT32_MAX) ++stats.drawn;
        stats.last_px[0] = note.px[0]; stats.last_px[1] = note.px[1];
        std::memcpy(stats.label_box, note.label_box, sizeof(stats.label_box));
        stats.alpha = note.alpha;
        break;
    default: break;
    }
    if (note.crosshair) {
        if (stats.crosshair_frames != UINT32_MAX) ++stats.crosshair_frames;
        std::memcpy(stats.ring, note.ring, sizeof(stats.ring));
    }
    ReleaseSRWLockExclusive(&lock);
}
RenderStats GetRenderStats() noexcept {
    AcquireSRWLockShared(&lock);
    const RenderStats copy = stats;
    ReleaseSRWLockShared(&lock);
    return copy;
}
void PublishTelemetry(const Telemetry& value) noexcept {
    AcquireSRWLockExclusive(&lock);
    telemetry = value;
    ReleaseSRWLockExclusive(&lock);
}
Telemetry GetTelemetry() noexcept {
    AcquireSRWLockShared(&lock);
    const Telemetry copy = telemetry;
    ReleaseSRWLockShared(&lock);
    return copy;
}
} // namespace mgscoop::nametag
