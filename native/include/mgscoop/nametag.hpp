#pragma once
// Partner name tag shared state (docs/partner-nametag.md): settings, the
// game-thread -> Present frame handoff, render counters and telemetry. Copies
// only; nothing here reads game memory or calls the game.
#include <cstdint>
#include "mgscoop/nametag_core.hpp"

namespace mgscoop::nametag {
struct Settings {
    bool on = true;
    Crosshair crosshair = Crosshair::aim;
    std::uint32_t max_m = 300;
};
// Install time (natural observer thread, before the overlay installs).
// allowed: an offline co-op partner session whose camera contract verified;
// the overlay hosts the layer only then. Any later contract refusal calls
// Refuse(), which hides everything for the rest of the process.
void Configure(bool allowed, bool host, const Settings& settings) noexcept;
bool Enabled() noexcept;
bool Host() noexcept;
void Refuse() noexcept;
Settings GetSettings() noexcept;
// In-memory toggles (F9 menu / text commands, game thread).
void SetOn(bool value) noexcept;
void SetCrosshair(Crosshair value) noexcept;

// Game thread: the newest frame (copied under a lock).
void Publish(const Frame& frame) noexcept;
// Present: false until a frame was published (or after Shutdown / Refuse).
bool Take(Frame& frame) noexcept;
// Present (worker builds): a fresh frame that draws something exists.
bool RenderPending(std::uint64_t now_ms) noexcept;
// The window is closing: hide at once (thread-safe flag).
void Shutdown() noexcept;
bool ShuttingDown() noexcept;

// Present: one call per overlay frame with what it did.
enum class Drawn : std::uint8_t { none, stale, hidden, drawn };
struct RenderNote {
    Drawn kind;
    bool crosshair;
    float px[2];
    float label_box[4];
    float ring[3];
    std::uint32_t bb[2], display[2];
    float alpha;
    std::uint32_t thread, age_ms;
};
void NoteRender(const RenderNote& note) noexcept;
RenderStats GetRenderStats() noexcept;

// Game thread telemetry.
void PublishTelemetry(const Telemetry& value) noexcept;
Telemetry GetTelemetry() noexcept;

// Clears the frame, the render counters and the telemetry (Configure; tests).
void ResetState() noexcept;
} // namespace mgscoop::nametag
