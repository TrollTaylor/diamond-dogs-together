#pragma once
#include "mgscoop/worker_scenario.hpp"
struct IDXGISwapChain;
namespace mgscoop::worker_frames {
// Called only from the game's natural Present callback. No screen, cursor,
// desktop switch, input injection, UI automation, or game function calls.
void Capture(IDXGISwapChain*, const worker_scenario::CaptureRequest&) noexcept;
void Reset() noexcept;
} // namespace mgscoop::worker_frames
