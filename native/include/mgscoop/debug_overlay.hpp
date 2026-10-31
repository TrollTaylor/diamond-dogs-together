#pragma once
#include <cstdint>
#include <cstddef>

// Original offline debug UI. Graphics/input only; game actions use debug_menu.
namespace mgscoop::debug_overlay {
// Called after the real DirectInput factory succeeds, outside loader lock.
void Install() noexcept;
// Called once, on the window's thread, at the game window's first WM_CLOSE /
// WM_DESTROY / WM_ENDSESSION (before the game handles it), so per-mission
// native hooks can disarm before the engine tears its objects down.
using ShutdownFn = void (*)() noexcept;
void SetShutdownHandler(ShutdownFn fn) noexcept;
// Also available to game-owned input wrappers. False on normal launches.
bool CapturingInput() noexcept;
// Process-local virtual controller provider, shared by the early IAT wrapper
// and XInput export hook. Private admission, exact ABI and F9 capture repeated.
bool WorkerVirtualPadState(unsigned index,void* output,std::size_t bytes) noexcept;

// Copy-only worker diagnostics: independent cumulative counters saturate at
// UINT32_MAX. Reads count successful synthetic RID_INPUT payload copies;
// packets_read counts each token once, excluding size/header queries.
// Posted counts require PostMessage success. DI buffer events include actual
// consumed events; peek events are counted separately. No gameplay acceptance
// is inferred from these API observations. DI calls count qualified mouse
// paths after a successful original call. Normal launches return zeroes.
struct WorkerInputStats {
    std::uint32_t raw_keyboard_posted{};
    std::uint32_t raw_mouse_posted{};
    std::uint32_t raw_post_failed{};
    std::uint32_t raw_mouse_axes_posted{};
    std::uint32_t raw_mouse_buttons_posted{};
    std::uint32_t raw_keyboard_reads{};
    std::uint32_t raw_mouse_reads{};
    std::uint32_t raw_mouse_axes_reads{};
    std::uint32_t raw_mouse_buttons_reads{};
    std::uint32_t raw_keyboard_packets_read{};
    std::uint32_t raw_mouse_packets_read{};
    std::uint32_t raw_mouse_axis_packets_read{};
    std::uint32_t raw_mouse_button_packets_read{};
    std::uint32_t raw_read_refused{};
    std::uint32_t cursor_success{};
    std::uint32_t cursor_null{};
    std::uint32_t cursor_thread_refused{};
    std::uint32_t cursor_window_refused{};
    std::uint32_t cursor_geometry_refused{};
    std::uint32_t cursor_map_refused{};
    std::uint32_t cursor_window_match{};
    std::uint32_t cursor_window_mismatch{};
    std::uint32_t cursor_present_missing{};
    std::uint32_t di_mouse_state_calls{};
    std::uint32_t di_mouse_state_supplied{};
    std::uint32_t di_mouse_state_axes{};
    std::uint32_t di_mouse_state_buttons{};
    std::uint32_t di_mouse_buffer_calls{};
    std::uint32_t di_mouse_buffer_events{};
    std::uint32_t di_mouse_buffer_peek_events{};
};
WorkerInputStats GetWorkerInputStats() noexcept;

#ifdef MGS_DEBUG_OVERLAY_TESTING
struct TestStats {
    unsigned presents;
    unsigned frames;
    unsigned resizes;
    unsigned input_messages;
    unsigned characters;
    bool initialized;
    bool open;
};
void SetOpenForTest(bool value) noexcept;
void SetForegroundForTest(bool value) noexcept;
void SetMouseHeldForTest(bool value) noexcept;
bool HookInputFixtureForTest(void* state, void* data) noexcept;
bool HookPadFixtureForTest(void* function) noexcept;
void RawKeyForTest(unsigned key) noexcept;
TestStats StatsForTest() noexcept;
// Copies the backbuffer (R8G8B8A8 only) right after the next overlay draw,
// before Present; the pixels stay valid until the next capture.
void CaptureNextFrameForTest() noexcept;
bool CapturedFrameForTest(const unsigned char*& rgba, unsigned& width, unsigned& height) noexcept;
void ShutdownForTest() noexcept;
// The install refusal line: the kept prefix 'overlay=refused; reason=graphics_or_input_hook' plus the
// failing step, its sub-step and code (kind 0 none, 1 HRESULT, 2 MH_STATUS, 3 GetLastError, 4 null
// address). False when it does not fit.
bool RefusalLineForTest(const char* step, const char* detail, unsigned kind, long code, char* out, std::size_t capacity) noexcept;
// The last install line this process logged (installed or refused); false before an install ran.
bool InstallLineForTest(char* out, std::size_t capacity) noexcept;
#endif
}
