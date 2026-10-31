// Original DX11 debug overlay. No game addresses, native world calls or Lua.
// Dear ImGui core/backends: MIT, pinned in third_party/imgui/UPSTREAM.txt.
// Also hosts the partner name tag layer (docs/partner-nametag.md): copied
// frames from nametag::Publish only, drawn under the menu.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dinput.h>
#include <xinput.h>
#include <strsafe.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <cstdlib>
#include <cfloat>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "mgscoop/debug_overlay.hpp"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_actions.hpp"
#include "mgscoop/debug_menu_ui.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "mgscoop/worker_frames.hpp"
#include "mgscoop/nametag.hpp"
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/steam_online_state.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace mgscoop::debug_overlay {
namespace {
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using InputCreateFn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using InputStateFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);
using InputDataFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using CursorPosFn = BOOL (WINAPI*)(int, int);
using GetCursorPosFn = BOOL (WINAPI*)(LPPOINT);
using ClientRectFn = BOOL (WINAPI*)(HWND, LPRECT);
using ClientToScreenFn = BOOL (WINAPI*)(HWND, LPPOINT);
using ClipFn = BOOL (WINAPI*)(const RECT*);
using KeyboardFn = BOOL (WINAPI*)(PBYTE);
using PadFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
using InputFormatFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, LPCDIDATAFORMAT);
using InputCoopFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, HWND, DWORD);
using AsyncFn = SHORT (WINAPI*)(int);
using KeyStateFn = SHORT (WINAPI*)(int);
using RawInputFn = UINT (WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using RawBufferFn = UINT (WINAPI*)(PRAWINPUT, PUINT, UINT);
struct Hook { void* target; void* original; };
struct Message { UINT id; WPARAM wparam; LPARAM lparam; };

// Constant-initialized state only. No DLL-detach destructors/graphics calls.
INIT_ONCE install_once = INIT_ONCE_STATIC_INIT;
SRWLOCK render_lock = SRWLOCK_INIT, message_lock = SRWLOCK_INIT, clip_lock = SRWLOCK_INIT;
std::atomic<bool> open{false}, releasing{false}, f9_down{false}, resizing{false};
std::atomic<HWND> window{nullptr};
std::atomic<ULONGLONG> release_after{0};
Hook present_hook{}, resize_hook{}, state_hooks[2]{}, data_hooks[2]{};
Hook cursor_hook{}, clip_hook{}, keyboard_hook{}, pad_hook{};
// The last local keyboard/mouse press and controller activity this game saw;
// the rescue prompt's button glyph follows whichever came last.
std::atomic<ULONGLONG> last_keyboard_ms{0}, last_pad_ms{0};
#if MGS_BACKGROUND_WORKER
enum class WorkerCounter : unsigned {
    raw_keyboard_posted,
    raw_mouse_posted,
    raw_post_failed,
    raw_mouse_axes_posted,
    raw_mouse_buttons_posted,
    raw_keyboard_reads,
    raw_mouse_reads,
    raw_mouse_axes_reads,
    raw_mouse_buttons_reads,
    raw_keyboard_packets_read,
    raw_mouse_packets_read,
    raw_mouse_axis_packets_read,
    raw_mouse_button_packets_read,
    raw_read_refused,
    cursor_success,
    cursor_null,
    cursor_thread_refused,
    cursor_window_refused,
    cursor_geometry_refused,
    cursor_map_refused,
    cursor_window_match,
    cursor_window_mismatch,
    cursor_present_missing,
    di_mouse_state_calls,
    di_mouse_state_supplied,
    di_mouse_state_axes,
    di_mouse_state_buttons,
    di_mouse_buffer_calls,
    di_mouse_buffer_events,
    di_mouse_buffer_peek_events,
    count
};
std::atomic<std::uint32_t> worker_counters[static_cast<unsigned>(WorkerCounter::count)]{};
void CountWorker(WorkerCounter field, std::uint32_t amount = 1) noexcept {
    auto& counter = worker_counters[static_cast<unsigned>(field)];
    auto value = counter.load(std::memory_order_relaxed);
    while (value != UINT32_MAX) {
        const auto next = amount > UINT32_MAX - value ? UINT32_MAX : value + amount;
        if (counter.compare_exchange_weak(value, next, std::memory_order_relaxed)) return;
    }
}
Hook format_hooks[2]{}, coop_hooks[2]{}, async_hook{}, key_state_hook{}, raw_hook{}, raw_buffer_hook{}, foreground_hook{}, get_cursor_hook{};
// The game's own window on the private desktop (validated SetCooperativeLevel
// target, or found by enumerating this thread's desktop).
std::atomic<HWND> worker_window{nullptr};
SRWLOCK worker_device_lock = SRWLOCK_INIT;
struct WorkerDevice { IDirectInputDevice8W* object{}; DWORD type{}, bytes{}; GUID instance{}; };
WorkerDevice worker_devices[32]{};
#endif
#ifdef MGS_DEBUG_OVERLAY_TESTING
Hook fixture_state{}, fixture_data{}, fixture_pad{};
#if MGS_BACKGROUND_WORKER && defined(MGS_WORKER_TESTING)
Hook fixture_format{}, fixture_coop{}, fixture_raw_buffer{}, fixture_raw_input{};
struct CursorFixture {
    bool enabled{};
    HWND window{};
    ClientRectFn rect{};
    ClientToScreenFn to_screen{};
    GetCursorPosFn original{};
};
CursorFixture fixture_cursor{};
#endif
std::atomic<bool> test_foreground{true};
std::atomic<bool> test_mouse_held{false};
#endif
PresentFn original_present = nullptr;
ResizeFn original_resize = nullptr;
std::atomic<WNDPROC> original_wndproc{nullptr};
IDXGISwapChain* swapchain = nullptr; // borrowed only during its callbacks
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
ID3D11RenderTargetView* target = nullptr;
ImGuiContext* gui = nullptr;
bool initialized = false, clip_saved = false, restore_unclipped = false, was_open = false, tag_logged = false, hud_logged = false;
RECT previous_clip{};
Message messages[256]{};
unsigned message_count = 0;
std::atomic<unsigned> presents{0}, frames{0}, resizes{0}, input_messages{0};
std::atomic<unsigned> characters{0};
HANDLE log_file = INVALID_HANDLE_VALUE;

void Log(const char* text) noexcept {
    if (log_file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(log_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}
void InitializeLog() noexcept {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return;
    wchar_t* last = std::wcsrchr(path, L'\\');
    if (!last) return;
    *last = 0;
    if (FAILED(StringCchCatW(path, ARRAYSIZE(path), L"\\mgs-coop\\debug-overlay.log"))) return;
    log_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}
void Push(UINT id, WPARAM wp, LPARAM lp) noexcept {
    AcquireSRWLockExclusive(&message_lock);
    if (message_count < ARRAYSIZE(messages)) messages[message_count++] = {id, wp, lp};
    ReleaseSRWLockExclusive(&message_lock);
    input_messages.fetch_add(1, std::memory_order_relaxed);
}
void NativeClip(const RECT* rect) noexcept {
    if (worker_scenario::Requested()) return;
#ifdef MGS_DEBUG_OVERLAY_TESTING
    (void)rect; // hidden tests never change the user's global cursor clip
#else
    if (clip_hook.original) reinterpret_cast<ClipFn>(clip_hook.original)(rect);
#endif
}
void SaveClip() noexcept {
    if (worker_scenario::Requested()) return;
    AcquireSRWLockExclusive(&clip_lock);
    if (!clip_saved) {
        clip_saved = GetClipCursor(&previous_clip) != FALSE;
        RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
        desktop.right = desktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        desktop.bottom = desktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        restore_unclipped = EqualRect(&desktop, &previous_clip) != FALSE;
    }
    ReleaseSRWLockExclusive(&clip_lock);
    NativeClip(nullptr);
}
void RestoreClip(HWND hwnd) noexcept {
    if (worker_scenario::Requested()) return;
    AcquireSRWLockExclusive(&clip_lock);
    const bool saved = clip_saved, no_clip = restore_unclipped;
    const RECT rect = previous_clip; clip_saved = false;
    ReleaseSRWLockExclusive(&clip_lock);
    // The clip is desktop-global. Never restore our old region over a newly
    // focused application's own cursor policy after Alt-Tab/focus loss.
    if (saved && hwnd && GetForegroundWindow() == hwnd) NativeClip(no_clip ? nullptr : &rect);
}
void ChangeOpen(bool value) noexcept {
    const bool old = open.exchange(value, std::memory_order_acq_rel);
    if (old == value) return;
    if (value) SaveClip();
    else {
        release_after.store(GetTickCount64() + 120, std::memory_order_release);
        releasing.store(true, std::memory_order_release);
    }
    Log(value ? "overlay=opened; key=F9; input=modal\r\n" : "overlay=closed; input=release_guard\r\n");
}
bool Foreground() noexcept {
    const HWND hwnd = window.load(std::memory_order_acquire);
#ifdef MGS_DEBUG_OVERLAY_TESTING
    return hwnd != nullptr && test_foreground.load(); // hidden fixture never steals user focus
#else
    return hwnd && GetForegroundWindow() == hwnd;
#endif
}
void F9(bool down) noexcept {
    if (worker_scenario::Requested()) return;
    const bool prior = f9_down.exchange(down, std::memory_order_acq_rel);
    if (down && !prior && debug_menu::Enabled() && Foreground()) ChangeOpen(!open.load(std::memory_order_acquire));
}
bool InputMessage(UINT id) noexcept {
    return (id >= WM_KEYFIRST && id <= WM_KEYLAST) ||
           (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST) || id == WM_INPUT;
}
#if MGS_BACKGROUND_WORKER
// MGSV reads keyboard and mouse only through WM_INPUT + GetRawInputData (two
// RAWINPUTDEVICEs registered at 0x1A45BC3; DirectInput serves pads). A private
// desktop receives no physical raw input that we let through, so the worker's
// scripted events become synthetic WM_INPUT messages posted to the game's own
// window; GetRawInputData answers their tokens from this store. Tokens carry a
// tag that no real HRAWINPUT has, and only tagged messages reach the game.
constexpr std::uintptr_t kRawTag = 0x7EA0000000000000ull, kRawTagMask = 0xFFFF000000000000ull;
struct SyntheticRaw { std::uintptr_t token{}; RAWINPUT raw{}; bool counted_read{};
    std::uint32_t loading_mission{},loading_generation{};std::uint64_t loading_expires{}; };
SRWLOCK synthetic_lock = SRWLOCK_INIT;
SyntheticRaw synthetic_ring[128]{};
std::uint32_t synthetic_next = 0;
bool SyntheticToken(std::uintptr_t value) noexcept { return (value & kRawTagMask) == kRawTag; }
bool SyntheticLookup(std::uintptr_t token, RAWINPUT& out) noexcept {
    AcquireSRWLockShared(&synthetic_lock);
    const auto& slot = synthetic_ring[token & 127];
    const bool found = slot.token == token;
    const auto mission=slot.loading_mission,generation=slot.loading_generation;
    const auto expires=slot.loading_expires;
    if (found) out = slot.raw;
    ReleaseSRWLockShared(&synthetic_lock);
    // A WM_INPUT token may already be posted when runtime entry revokes it.
    // Replace only this guarded loading down with a Space release; normal
    // scenario input and its original raw packet shape remain unchanged.
    if(found&&generation&&!worker_scenario::LoadingDeliveryAllowed(mission,generation,expires,GetTickCount64())) {
        out.data.keyboard.Flags|=RI_KEY_BREAK;out.data.keyboard.Message=WM_KEYUP;
    }
    return found;
}
std::uintptr_t SyntheticStore(const RAWINPUT& raw,const worker_scenario::Event* event=nullptr) noexcept {
    AcquireSRWLockExclusive(&synthetic_lock);
    const std::uint32_t n = ++synthetic_next;
    const std::uintptr_t token = kRawTag | n;
    synthetic_ring[n & 127] = {token, raw, false,event?event->loading_mission:0u,
        event?event->loading_generation:0u,event?event->expires:0ull};
    ReleaseSRWLockExclusive(&synthetic_lock); return token;
}
bool SyntheticFirstRead(std::uintptr_t token) noexcept {
    AcquireSRWLockExclusive(&synthetic_lock);
    auto& slot = synthetic_ring[token & 127];
    const bool first = slot.token == token && !slot.counted_read;
    if (first) slot.counted_read = true;
    ReleaseSRWLockExclusive(&synthetic_lock); return first;
}
void CountSyntheticRead(std::uintptr_t token, const RAWINPUT& raw) noexcept {
    const DWORD error = GetLastError();
    const bool first = SyntheticFirstRead(token);
    if (raw.header.dwType == RIM_TYPEKEYBOARD) {
        CountWorker(WorkerCounter::raw_keyboard_reads);
        if (first) CountWorker(WorkerCounter::raw_keyboard_packets_read);
    } else if (raw.header.dwType == RIM_TYPEMOUSE) {
        CountWorker(WorkerCounter::raw_mouse_reads);
        if (first) CountWorker(WorkerCounter::raw_mouse_packets_read);
        if (raw.data.mouse.lLastX || raw.data.mouse.lLastY) {
            CountWorker(WorkerCounter::raw_mouse_axes_reads);
            if (first) CountWorker(WorkerCounter::raw_mouse_axis_packets_read);
        }
        if (raw.data.mouse.usButtonFlags) {
            CountWorker(WorkerCounter::raw_mouse_buttons_reads);
            if (first) CountWorker(WorkerCounter::raw_mouse_button_packets_read);
        }
    }
    SetLastError(error);
}
void PostSynthetic(HWND game, const RAWINPUT& raw,const worker_scenario::Event* event=nullptr) noexcept {
    const BOOL posted = PostMessageW(game, WM_INPUT, RIM_INPUT, static_cast<LPARAM>(SyntheticStore(raw,event)));
    if (!posted) { CountWorker(WorkerCounter::raw_post_failed); return; }
    if (raw.header.dwType == RIM_TYPEKEYBOARD) CountWorker(WorkerCounter::raw_keyboard_posted);
    else {
        CountWorker(WorkerCounter::raw_mouse_posted);
        if (raw.data.mouse.lLastX || raw.data.mouse.lLastY) CountWorker(WorkerCounter::raw_mouse_axes_posted);
        if (raw.data.mouse.usButtonFlags) CountWorker(WorkerCounter::raw_mouse_buttons_posted);
    }
}
// Called on the admitted render thread (Present) with the game's own window.
RAWINPUT SyntheticKeyboard(const worker_scenario::Event& event) noexcept {
    RAWINPUT raw{};
    const UINT scan = event.offset & 0xFF;
    const bool down = (event.value & 0x80) != 0;
    raw.header.dwType = RIM_TYPEKEYBOARD;
    raw.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
    raw.header.hDevice = reinterpret_cast<HANDLE>(kRawTag | 1);
    raw.header.wParam = RIM_INPUT;
    raw.data.keyboard.MakeCode = static_cast<USHORT>(scan & 0x7F);
    raw.data.keyboard.Flags = static_cast<USHORT>((down ? RI_KEY_MAKE : RI_KEY_BREAK) | ((scan & 0x80) ? RI_KEY_E0 : 0));
    // Raw keyboard packets use generic modifier VKeys; scan code/E0 retain
    // the side. VK_LSHIFT/VK_LCONTROL from MAPVK_VSC_TO_VK_EX are key-state
    // API values, rather than the corresponding legacy raw-packet VKeys.
    raw.data.keyboard.VKey = static_cast<USHORT>(MapVirtualKeyW((scan & 0x7F) | ((scan & 0x80) ? 0xE000 : 0), MAPVK_VSC_TO_VK));
    raw.data.keyboard.Message = down ? WM_KEYDOWN : WM_KEYUP;
    return raw;
}
void PumpSyntheticInput(HWND game) noexcept {
    if (!game) return;
    worker_scenario::Event events[64]{};
    std::uint32_t count = worker_scenario::Buffered(worker_scenario::Device::keyboard, events, 64, false);
    for (std::uint32_t i = 0; i < count; ++i) {
        const RAWINPUT raw = SyntheticKeyboard(events[i]);
        PostSynthetic(game, raw,&events[i]);
    }
    count = worker_scenario::Buffered(worker_scenario::Device::mouse, events, 64, false);
    for (std::uint32_t i = 0; i < count; ++i) {
        RAWINPUT raw{}; const bool down = (events[i].value & 0x80) != 0;
        raw.header.dwType = RIM_TYPEMOUSE; raw.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
        raw.header.hDevice = reinterpret_cast<HANDLE>(kRawTag | 2); raw.header.wParam = RIM_INPUT;
        raw.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
        switch (events[i].offset) {
        case 0: raw.data.mouse.lLastX = static_cast<LONG>(static_cast<std::int32_t>(events[i].value)); break;
        case 4: raw.data.mouse.lLastY = static_cast<LONG>(static_cast<std::int32_t>(events[i].value)); break;
        case 12: raw.data.mouse.usButtonFlags = down ? RI_MOUSE_LEFT_BUTTON_DOWN : RI_MOUSE_LEFT_BUTTON_UP; break;
        case 13: raw.data.mouse.usButtonFlags = down ? RI_MOUSE_RIGHT_BUTTON_DOWN : RI_MOUSE_RIGHT_BUTTON_UP; break;
        default: continue;
        }
        PostSynthetic(game, raw);
    }
}
#endif
void RawMouse(const RAWMOUSE& mouse) noexcept {
    struct Button { USHORT down, up; UINT press, release; WPARAM wp; };
    constexpr Button buttons[] = {
        {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, WM_LBUTTONDOWN, WM_LBUTTONUP, 0},
        {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, WM_RBUTTONDOWN, WM_RBUTTONUP, 0},
        {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, WM_MBUTTONDOWN, WM_MBUTTONUP, 0},
        {RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, WM_XBUTTONDOWN, WM_XBUTTONUP, static_cast<WPARAM>(XBUTTON1) << 16},
        {RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, WM_XBUTTONDOWN, WM_XBUTTONUP, static_cast<WPARAM>(XBUTTON2) << 16}
    };
    for (const auto& b : buttons) {
        if (mouse.usButtonFlags & b.down) Push(b.press, b.wp, 0);
        if (mouse.usButtonFlags & b.up) Push(b.release, b.wp, 0);
    }
    if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
        Push(WM_MOUSEWHEEL, static_cast<WPARAM>(mouse.usButtonData) << 16, 0);
    if (mouse.usButtonFlags & RI_MOUSE_HWHEEL)
        Push(WM_MOUSEHWHEEL, static_cast<WPARAM>(mouse.usButtonData) << 16, 0);
}
void RawText(const RAWKEYBOARD& key) noexcept {
    if ((key.Flags & RI_KEY_BREAK) || key.VKey >= 256) return;
    RAWINPUTDEVICE devices[32]{}; UINT count = ARRAYSIZE(devices);
    const UINT read = GetRegisteredRawInputDevices(devices, &count, sizeof(RAWINPUTDEVICE));
    if (read == static_cast<UINT>(-1)) return;
    bool no_legacy = false;
    for (UINT i = 0; i < read; ++i)
        if (devices[i].usUsagePage == 1 && devices[i].usUsage == 6 &&
            (devices[i].dwFlags & RIDEV_NOLEGACY) == RIDEV_NOLEGACY) no_legacy = true;
    if (!no_legacy) return; // WM_CHAR already supplies text on legacy paths.
    BYTE keys[256]{};
    reinterpret_cast<KeyboardFn>(keyboard_hook.original)(keys);
    keys[key.VKey] |= 0x80;
    wchar_t text[4]{};
    // Bit2 prevents changing Windows dead-key/keyboard state (Windows10+).
    const int length = ToUnicodeEx(key.VKey, key.MakeCode, keys, text, ARRAYSIZE(text), 4, GetKeyboardLayout(0));
    for (int i = 0; i < length && i < static_cast<int>(ARRAYSIZE(text)); ++i) {
        if (text[i] >= 32) { Push(WM_CHAR, text[i], 0); characters.fetch_add(1, std::memory_order_relaxed); }
    }
}
std::atomic<ShutdownFn> shutdown_handler{nullptr};
std::atomic<bool> shutdown_fired{false};
LRESULT CALLBACK WindowProc(HWND hwnd, UINT id, WPARAM wp, LPARAM lp) noexcept {
    const WNDPROC previous = original_wndproc.load(std::memory_order_acquire);
    if ((id == WM_CLOSE || id == WM_DESTROY || (id == WM_ENDSESSION && wp)) && !shutdown_fired.exchange(true, std::memory_order_acq_rel))
        if (const ShutdownFn fn = shutdown_handler.load(std::memory_order_acquire)) fn();
    if (worker_scenario::Requested() && InputMessage(id)) {
#if MGS_BACKGROUND_WORKER
        if (id == WM_INPUT && SyntheticToken(static_cast<std::uintptr_t>(lp)) && previous)
            return CallWindowProcW(previous, hwnd, id, wp, lp);
#endif
        return id == WM_INPUT ? DefWindowProcW(hwnd, id, wp, lp) : 0;
    }
    bool toggle_event = false;
    if ((id == WM_KEYDOWN || id == WM_SYSKEYDOWN || id == WM_KEYUP || id == WM_SYSKEYUP) && wp == VK_F9) {
        F9(id == WM_KEYDOWN || id == WM_SYSKEYDOWN);
        toggle_event = debug_menu::Enabled() && Foreground();
    }
    if (id == WM_KEYDOWN || id == WM_SYSKEYDOWN || id == WM_LBUTTONDOWN || id == WM_RBUTTONDOWN)
        last_keyboard_ms.store(GetTickCount64(), std::memory_order_relaxed);
    if (id == WM_INPUT) {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1)) {
            if ((raw.header.dwType == RIM_TYPEKEYBOARD && (raw.data.keyboard.Flags & RI_KEY_BREAK) == 0) ||
                (raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usButtonFlags &
                    (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_DOWN))))
                last_keyboard_ms.store(GetTickCount64(), std::memory_order_relaxed);
            if (raw.header.dwType == RIM_TYPEKEYBOARD && raw.data.keyboard.VKey == VK_F9) {
                F9((raw.data.keyboard.Flags & RI_KEY_BREAK) == 0);
                toggle_event = debug_menu::Enabled() && Foreground();
            }
            if (CapturingInput()) {
                if (raw.header.dwType == RIM_TYPEMOUSE) RawMouse(raw.data.mouse);
                else if (raw.header.dwType == RIM_TYPEKEYBOARD) {
                    const RAWKEYBOARD& key = raw.data.keyboard;
                    const bool up = (key.Flags & RI_KEY_BREAK) != 0;
                    LPARAM flags = static_cast<LPARAM>(key.MakeCode) << 16;
                    if (key.Flags & RI_KEY_E0) flags |= 1LL << 24;
                    if (up) flags |= 3LL << 30;
                    Push(up ? WM_KEYUP : WM_KEYDOWN, key.VKey, flags);
                    RawText(key);
                }
            }
        }
    }
    if (id == WM_KILLFOCUS || id == WM_DESTROY) {
        // A worker-opened menu stays open through private-desktop focus churn.
        if (!(worker_scenario::Requested() && id == WM_KILLFOCUS)) ChangeOpen(false);
        f9_down.store(false, std::memory_order_release);
    }
    if (id != WM_INPUT) Push(id, wp, lp);
    if (toggle_event || (CapturingInput() && InputMessage(id))) {
        // Foreground raw input needs DefWindowProc cleanup even when consumed.
        return id == WM_INPUT ? DefWindowProcW(hwnd, id, wp, lp) : 0;
    }
    if (CapturingInput() && id == WM_SETCURSOR) return TRUE;
    return previous ? CallWindowProcW(previous, hwnd, id, wp, lp) : DefWindowProcW(hwnd, id, wp, lp);
}

bool AddHook(Hook& hook, void* address, void* replacement) noexcept {
    if (!address || MH_CreateHook(address, replacement, &hook.original) != MH_OK) return false;
    hook.target = address;
    return true;
}
// Review m6m7g: the first failing install step, its sub-step and code, appended to the refusal line after
// the kept prefix 'overlay=refused; reason=graphics_or_input_hook' (InstallOnce runs once under
// install_once; the test fixtures' AddHook calls never record).
enum class FailKind : unsigned { none = 0, hr = 1, mh = 2, win32 = 3, null_address = 4 };
struct InstallFailure { const char* step; const char* detail; FailKind kind; long code; };
InstallFailure install_failure{};
char install_line[256]{};
void Fail(const char* step, const char* detail, FailKind kind, long code) noexcept {
    if (!install_failure.step) install_failure = {step, detail, kind, code};
}
bool RefusalLine(const InstallFailure& failure, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    const char* const prefix = "overlay=refused; reason=graphics_or_input_hook";
    HRESULT written = S_OK;
    if (!failure.step) {
        written = StringCchPrintfA(out, capacity, "%s; step=unknown\r\n", prefix);
    } else {
        char code[80] = "";
        switch (failure.kind) {
        case FailKind::hr: StringCchPrintfA(code, ARRAYSIZE(code), "; hr=0x%08lX", static_cast<unsigned long>(failure.code)); break;
        case FailKind::mh: StringCchPrintfA(code, ARRAYSIZE(code), "; mh=%s(%ld)", MH_StatusToString(static_cast<MH_STATUS>(failure.code)), failure.code); break;
        case FailKind::win32: StringCchPrintfA(code, ARRAYSIZE(code), "; error=%lu", static_cast<unsigned long>(failure.code)); break;
        case FailKind::null_address: StringCchCopyA(code, ARRAYSIZE(code), "; address=null"); break;
        default: break;
        }
        written = failure.detail ? StringCchPrintfA(out, capacity, "%s; step=%s; detail=%s%s\r\n", prefix, failure.step, failure.detail, code)
                                 : StringCchPrintfA(out, capacity, "%s; step=%s%s\r\n", prefix, failure.step, code);
    }
    if (FAILED(written)) { out[0] = 0; return false; }
    return true;
}
// AddHook for an install step: a null address or a MinHook failure is recorded with its name and MH_STATUS.
bool StepHook(const char* step, const char* detail, Hook& hook, void* address, void* replacement) noexcept {
    if (!address) { Fail(step, detail, FailKind::null_address, 0); return false; }
    const MH_STATUS status = MH_CreateHook(address, replacement, &hook.original);
    if (status != MH_OK) { Fail(step, detail, FailKind::mh, status); return false; }
    hook.target = address;
    return true;
}
bool QueueHook(const Hook& hook, const char* detail, bool required) noexcept {
    if (!hook.target) {
        if (required) Fail("enable_hooks", detail, FailKind::null_address, 0);
        return !required;
    }
    const MH_STATUS status = MH_QueueEnableHook(hook.target);
    if (status != MH_OK) { Fail("enable_hooks", detail, FailKind::mh, status); return false; }
    return true;
}
void RemoveHook(Hook& hook) noexcept {
    if (hook.target) { MH_DisableHook(hook.target); MH_RemoveHook(hook.target); }
    hook = {};
}
void RemoveHooks() noexcept {
    RemoveHook(present_hook); RemoveHook(resize_hook);
    for (auto& hook : state_hooks) RemoveHook(hook);
    for (auto& hook : data_hooks) RemoveHook(hook);
    RemoveHook(cursor_hook); RemoveHook(clip_hook); RemoveHook(keyboard_hook); RemoveHook(pad_hook);
#if MGS_BACKGROUND_WORKER
    for (auto& hook : format_hooks) RemoveHook(hook);
    for (auto& hook : coop_hooks) RemoveHook(hook);
    RemoveHook(async_hook);
    RemoveHook(key_state_hook);
    RemoveHook(raw_hook);
    RemoveHook(raw_buffer_hook);
    RemoveHook(foreground_hook);
    RemoveHook(get_cursor_hook);
#endif
}
bool EnableHooks() noexcept {
    const Hook* hooks[] = {&present_hook, &resize_hook, &state_hooks[0], &state_hooks[1],
                    &data_hooks[0], &data_hooks[1], &cursor_hook, &clip_hook, &keyboard_hook, &pad_hook};
    const char* const names[] = {"Present", "ResizeBuffers", "keyboard_GetDeviceState", "mouse_GetDeviceState",
                    "keyboard_GetDeviceData", "mouse_GetDeviceData", "SetCursorPos", "ClipCursor", "GetKeyboardState", "XInputGetState"};
    static_assert(ARRAYSIZE(hooks) == ARRAYSIZE(names), "one name per hook");
    for (unsigned i = 0; i < ARRAYSIZE(hooks); ++i)
        if (!QueueHook(*hooks[i], names[i], false)) return false;
#if MGS_BACKGROUND_WORKER
    if (worker_scenario::Requested()) {
        if (!QueueHook(format_hooks[0], "keyboard_SetDataFormat", false) || !QueueHook(format_hooks[1], "mouse_SetDataFormat", false) ||
            !QueueHook(coop_hooks[0], "keyboard_SetCooperativeLevel", false) || !QueueHook(coop_hooks[1], "mouse_SetCooperativeLevel", false) ||
            !QueueHook(async_hook, "GetAsyncKeyState", true) || !QueueHook(key_state_hook, "GetKeyState", true) ||
            !QueueHook(raw_hook, "GetRawInputData", true) || !QueueHook(raw_buffer_hook, "GetRawInputBuffer", true) ||
            !QueueHook(foreground_hook, "GetForegroundWindow", true) || !QueueHook(get_cursor_hook, "GetCursorPos", true)) return false;
    }
#endif
    const MH_STATUS applied = MH_ApplyQueued();
    if (applied != MH_OK) { Fail("enable_hooks", "apply_queued", FailKind::mh, applied); return false; }
    return true;
}
template<class Function>
Function DeviceOriginal(IDirectInputDevice8W* self, unsigned index, const Hook (&hooks)[2]) noexcept {
    void* address = (*reinterpret_cast<void***>(self))[index];
    for (const auto& hook : hooks)
        if (hook.target == address) return reinterpret_cast<Function>(hook.original);
#ifdef MGS_DEBUG_OVERLAY_TESTING
    if (index == 9 && fixture_state.target == address) return reinterpret_cast<Function>(fixture_state.original);
    if (index == 10 && fixture_data.target == address) return reinterpret_cast<Function>(fixture_data.original);
#if MGS_BACKGROUND_WORKER && defined(MGS_WORKER_TESTING)
    if (index == 11 && fixture_format.target == address) return reinterpret_cast<Function>(fixture_format.original);
    if (index == 13 && fixture_coop.target == address) return reinterpret_cast<Function>(fixture_coop.original);
#endif
#endif
    return nullptr;
}
bool KeyboardOrMouse(IDirectInputDevice8W* self) noexcept {
    DIDEVICEINSTANCEW info{}; info.dwSize = sizeof(info);
    if (FAILED(self->GetDeviceInfo(&info))) return false;
    const DWORD type = GET_DIDEVICE_TYPE(info.dwDevType);
    return type == DI8DEVTYPE_KEYBOARD || type == DI8DEVTYPE_MOUSE;
}
#if MGS_BACKGROUND_WORKER
bool WorkerInfo(IDirectInputDevice8W* self, DIDEVICEINSTANCEW& info) noexcept {
    info = {}; info.dwSize = sizeof(info);
    return self && SUCCEEDED(self->GetDeviceInfo(&info)) &&
        (GET_DIDEVICE_TYPE(info.dwDevType) == DI8DEVTYPE_KEYBOARD || GET_DIDEVICE_TYPE(info.dwDevType) == DI8DEVTYPE_MOUSE);
}
bool EqualFormat(LPCDIDATAFORMAT input, const DIDATAFORMAT& expected) noexcept {
    if (!input || input->dwSize != sizeof(DIDATAFORMAT) || input->dwObjSize != sizeof(DIOBJECTDATAFORMAT) ||
        input->dwFlags != expected.dwFlags || input->dwDataSize != expected.dwDataSize || input->dwNumObjs != expected.dwNumObjs ||
        !input->rgodf || input->dwNumObjs > 256) return false;
    for (DWORD i = 0; i < input->dwNumObjs; ++i) {
        const auto& a = input->rgodf[i]; const auto& b = expected.rgodf[i];
        if (a.dwOfs != b.dwOfs || a.dwType != b.dwType || a.dwFlags != b.dwFlags ||
            (!a.pguid != !b.pguid) || (a.pguid && !IsEqualGUID(*a.pguid, *b.pguid))) return false;
    }
    return true;
}
bool WorkerTyped(IDirectInputDevice8W* self, DWORD bytes, DWORD& type) noexcept {
    DIDEVICEINSTANCEW info{}; if (!WorkerInfo(self, info)) return false;
    bool valid = false; AcquireSRWLockShared(&worker_device_lock);
    for (const auto& value : worker_devices) if (value.object == self) {
        type = value.type; valid = bytes == value.bytes && value.type == GET_DIDEVICE_TYPE(info.dwDevType) && IsEqualGUID(value.instance, info.guidInstance); break;
    }
    ReleaseSRWLockShared(&worker_device_lock); return valid;
}
HRESULT STDMETHODCALLTYPE WorkerFormat(IDirectInputDevice8W* self, LPCDIDATAFORMAT format) noexcept {
    const auto original = DeviceOriginal<InputFormatFn>(self, 11, format_hooks);
    if (!original) return DIERR_GENERIC;
    const HRESULT result = original(self, format); const DWORD error = GetLastError();
    if (worker_scenario::Requested()) {
        DIDEVICEINSTANCEW info{}; const bool known = WorkerInfo(self, info);
        const DWORD type = known ? GET_DIDEVICE_TYPE(info.dwDevType) : 0;
        const bool typed = SUCCEEDED(result) && known && worker_scenario::ThreadAdmitted() &&
            (type == DI8DEVTYPE_KEYBOARD ? EqualFormat(format, c_dfDIKeyboard) :
             (EqualFormat(format, c_dfDIMouse) || EqualFormat(format, c_dfDIMouse2)));
        AcquireSRWLockExclusive(&worker_device_lock);
        WorkerDevice* slot = nullptr;
        for (auto& value : worker_devices) if (value.object == self) { slot = &value; break; }
        if (!slot) for (auto& value : worker_devices) if (!value.object) { slot = &value; break; }
        if (slot) *slot = {self, typed ? type : 0, typed ? format->dwDataSize : 0, info.guidInstance};
        ReleaseSRWLockExclusive(&worker_device_lock);
        worker_scenario::DeviceConfiguration("SetDataFormat", result, typed && slot, 0, 0);
    }
    SetLastError(error); return result;
}
HRESULT STDMETHODCALLTYPE WorkerCoop(IDirectInputDevice8W* self, HWND hwnd, DWORD flags) noexcept {
    const auto original = DeviceOriginal<InputCoopFn>(self, 13, coop_hooks);
    if (!original) return DIERR_GENERIC;
    const DWORD incoming_error = GetLastError();
    DWORD forwarded = flags; bool qualified = false;
    if (worker_scenario::Requested()) {
#ifdef MGS_WORKER_TESTING
        DIDEVICEINSTANCEW info{};
        const bool valid = worker_scenario::ThreadAdmitted() && WorkerInfo(self, info);
#else
        DWORD pid = 0; const DWORD thread = GetWindowThreadProcessId(hwnd, &pid);
        wchar_t desktop[256]{}; DWORD needed = 0; DIDEVICEINSTANCEW info{};
        const auto run = worker_scenario::GetContext();
        const bool valid = worker_scenario::ThreadAdmitted() && WorkerInfo(self, info) && pid == run.pid && thread &&
            GetUserObjectInformationW(GetThreadDesktop(thread), UOI_NAME, desktop, sizeof(desktop), &needed) &&
            needed <= sizeof(desktop) && !std::wcscmp(desktop, run.desktop);
#endif
        if (valid) { forwarded = DISCL_NONEXCLUSIVE | DISCL_BACKGROUND; qualified = true; worker_window.store(hwnd); }
        else {
            DIDEVICEINSTANCEW rejected_info{};
            if (WorkerInfo(self, rejected_info)) worker_scenario::Refuse("cooperative_private_window_or_thread");
        }
    }
    SetLastError(incoming_error);
    const HRESULT result = original(self, hwnd, forwarded); const DWORD error = GetLastError();
    if (worker_scenario::Requested()) worker_scenario::DeviceConfiguration("SetCooperativeLevel", result, qualified, flags, forwarded);
    SetLastError(error); return result;
}
// MGSV only reads input while GetForegroundWindow() is its own window. A window
// on a never-displayed desktop is never the input desktop's foreground, so an
// admitted game thread on the private desktop is answered with the game's own
// private window. This is a process-local answer: no focus, activation or
// SetForegroundWindow call is made and the user's desktop is never touched.
bool PrivateWorkerWindow(HWND hwnd) noexcept {
    DWORD pid = 0; const DWORD thread = GetWindowThreadProcessId(hwnd, &pid);
    const auto run = worker_scenario::GetContext();
    if (!thread || pid != GetCurrentProcessId() || pid != run.pid || !IsWindow(hwnd) ||
        !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) || !run.desktop || !*run.desktop) return false;
    wchar_t name[64]{};
    if (!GetClassNameW(hwnd, name, ARRAYSIZE(name)) || !std::wcscmp(name, L"MGSCoopDX11Probe")) return false;
    wchar_t desktop[256]{}; DWORD needed = 0;
    return GetUserObjectInformationW(GetThreadDesktop(thread), UOI_NAME, desktop, sizeof(desktop), &needed) &&
        needed <= sizeof(desktop) && !std::wcscmp(desktop, run.desktop);
}
BOOL CALLBACK FindWorkerWindow(HWND hwnd, LPARAM out) noexcept {
    if (!PrivateWorkerWindow(hwnd)) return TRUE;
    *reinterpret_cast<HWND*>(out) = hwnd; return FALSE;
}
HWND CurrentWorkerWindow() noexcept {
    HWND own = worker_window.load();
    if (!own || !PrivateWorkerWindow(own)) {
        own = nullptr; EnumWindows(&FindWorkerWindow, reinterpret_cast<LPARAM>(&own));
        worker_window.store(own);
    }
    return own;
}
HWND WINAPI WorkerForeground() noexcept {
    const DWORD error = GetLastError();
    if (worker_scenario::Requested() && worker_scenario::ThreadAdmitted()) {
        const HWND own = CurrentWorkerWindow();
        if (own) { SetLastError(error); return own; }
    }
    SetLastError(error);
    return reinterpret_cast<HWND (WINAPI*)()>(foreground_hook.original)();
}
// Retail raw-mouse ingestion rejects axes when GetCursorPos fails and rejects
// button-down outside its client rectangle. A private desktop cannot query the
// input desktop's cursor. Supply only a local, qualified window centre; neither
// this getter nor the worker's SetCursorPos wrapper touches the real cursor.
BOOL WINAPI WorkerGetCursorPos(LPPOINT point) noexcept {
    const DWORD error = GetLastError();
    if (worker_scenario::Requested()) {
        if (!point) { CountWorker(WorkerCounter::cursor_null); SetLastError(error); return FALSE; }
        if (!worker_scenario::ThreadAdmitted()) { CountWorker(WorkerCounter::cursor_thread_refused); SetLastError(error); return FALSE; }
        ClientRectFn rect = &GetClientRect;
        ClientToScreenFn to_screen = &ClientToScreen;
        HWND own = nullptr;
#if defined(MGS_WORKER_TESTING) && defined(MGS_DEBUG_OVERLAY_TESTING)
        if (fixture_cursor.enabled) {
            own = fixture_cursor.window; rect = fixture_cursor.rect; to_screen = fixture_cursor.to_screen;
        } else
#endif
        { own = CurrentWorkerWindow(); }
        RECT client{};
        if (!own) { CountWorker(WorkerCounter::cursor_window_refused); SetLastError(error); return FALSE; }
        const HWND present_window = window.load();
        CountWorker(!present_window ? WorkerCounter::cursor_present_missing :
                    own == present_window ? WorkerCounter::cursor_window_match : WorkerCounter::cursor_window_mismatch);
        if (!rect || !to_screen || !rect(own, &client) ||
            static_cast<std::int64_t>(client.right) - client.left < 2 ||
            static_cast<std::int64_t>(client.bottom) - client.top < 2) {
            CountWorker(WorkerCounter::cursor_geometry_refused); SetLastError(error); return FALSE;
        }
        POINT centre{
            static_cast<LONG>(static_cast<std::int64_t>(client.left) + (static_cast<std::int64_t>(client.right) - client.left) / 2),
            static_cast<LONG>(static_cast<std::int64_t>(client.top) + (static_cast<std::int64_t>(client.bottom) - client.top) / 2)};
        if (!to_screen(own, &centre)) { CountWorker(WorkerCounter::cursor_map_refused); SetLastError(error); return FALSE; }
        CountWorker(WorkerCounter::cursor_success); *point = centre; SetLastError(error); return TRUE;
    }
    auto original = reinterpret_cast<GetCursorPosFn>(get_cursor_hook.original);
#if defined(MGS_WORKER_TESTING) && defined(MGS_DEBUG_OVERLAY_TESTING)
    if (fixture_cursor.enabled) original = fixture_cursor.original;
#endif
    SetLastError(error);
    return original ? original(point) : FALSE;
}
SHORT WINAPI WorkerAsync(int key) noexcept {
    if (worker_scenario::Requested()) return 0; // physical/global state is never queried for worker scripts
    return reinterpret_cast<AsyncFn>(async_hook.original)(key);
}
SHORT WINAPI WorkerKeyState(int key) noexcept {
    if (worker_scenario::Requested()) return 0;
    return reinterpret_cast<KeyStateFn>(key_state_hook.original)(key);
}
UINT WINAPI WorkerRawBuffer(PRAWINPUT output, PUINT size, UINT header_size) noexcept {
    void* entry = raw_buffer_hook.original;
#if defined(MGS_WORKER_TESTING) && defined(MGS_DEBUG_OVERLAY_TESTING)
    if (fixture_raw_buffer.original) entry = fixture_raw_buffer.original;
#endif
    if (!entry) return static_cast<UINT>(-1);
    const UINT capacity = size ? *size : 0;
    const UINT result = reinterpret_cast<RawBufferFn>(entry)(output, size, header_size);
    const DWORD error = GetLastError();
    if (worker_scenario::Requested() && result != static_cast<UINT>(-1)) {
        // The API returns a record count, not a byte count. Do not interpret
        // unknown variable records. Ignore all successful physical batches.
        if (output && size && capacity <= 65536) std::memset(output, 0, capacity);
        SetLastError(error); return 0;
    }
    // Failure preserves the original output, size, UINT sentinel and error.
    SetLastError(error); return result;
}
UINT WINAPI WorkerRawInput(HRAWINPUT input, UINT command, LPVOID output, PUINT size, UINT header_size) noexcept {
    void* entry = raw_hook.original;
#if defined(MGS_WORKER_TESTING) && defined(MGS_DEBUG_OVERLAY_TESTING)
    if (fixture_raw_input.original) entry = fixture_raw_input.original;
#endif
    if (!entry) return static_cast<UINT>(-1);
#if MGS_BACKGROUND_WORKER
    if (worker_scenario::Requested() && SyntheticToken(reinterpret_cast<std::uintptr_t>(input))) {
        RAWINPUT raw{};
        if (!size || header_size != sizeof(RAWINPUTHEADER) || (command != RID_INPUT && command != RID_HEADER) ||
            !SyntheticLookup(reinterpret_cast<std::uintptr_t>(input), raw)) {
            CountWorker(WorkerCounter::raw_read_refused); SetLastError(ERROR_INVALID_HANDLE); return static_cast<UINT>(-1);
        }
        const UINT needed = command == RID_HEADER ? static_cast<UINT>(sizeof(RAWINPUTHEADER)) : raw.header.dwSize;
        if (!output) { *size = needed; return 0; }
        if (*size < needed) {
            CountWorker(WorkerCounter::raw_read_refused); *size = needed; SetLastError(ERROR_INSUFFICIENT_BUFFER); return static_cast<UINT>(-1);
        }
        std::memcpy(output, &raw, needed);
        if (command == RID_INPUT) CountSyntheticRead(reinterpret_cast<std::uintptr_t>(input), raw);
        return needed;
    }
#endif
    const UINT capacity = size ? *size : 0;
    const UINT result = reinterpret_cast<RawInputFn>(entry)(input, command, output, size, header_size);
    const DWORD error = GetLastError();
    if (worker_scenario::Requested() && result != static_cast<UINT>(-1) && output && command == RID_INPUT) {
        const bool bounded = size && header_size == sizeof(RAWINPUTHEADER) && result >= sizeof(RAWINPUTHEADER) &&
            result <= 65536 && result <= capacity && result <= *size && *size <= capacity;
        if (!bounded) {
            worker_scenario::InputFailure("GetRawInputData_worker_refusal", DIERR_UNSUPPORTED, true);
            SetLastError(error); return static_cast<UINT>(-1);
        }
        auto* raw = static_cast<RAWINPUT*>(output);
        const bool typed = raw->header.dwSize >= sizeof(RAWINPUTHEADER) && raw->header.dwSize <= result &&
            ((raw->header.dwType == RIM_TYPEMOUSE && result >= sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE)) ||
             (raw->header.dwType == RIM_TYPEKEYBOARD && result >= sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) ||
             (raw->header.dwType == RIM_TYPEHID && result >= sizeof(RAWINPUTHEADER) + offsetof(RAWHID,bRawData)));
        if (!typed) {
            worker_scenario::InputFailure("GetRawInputData_worker_refusal", DIERR_UNSUPPORTED, true);
            SetLastError(error); return static_cast<UINT>(-1);
        }
        if (raw->header.dwType == RIM_TYPEMOUSE) raw->data.mouse = {};
        else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            raw->data.keyboard = {}; raw->data.keyboard.Flags = RI_KEY_BREAK;
        } else if (result > sizeof(RAWINPUTHEADER))
            std::memset(static_cast<unsigned char*>(output) + sizeof(RAWINPUTHEADER), 0, result - sizeof(RAWINPUTHEADER));
    }
    SetLastError(error); return result;
}
#endif
HRESULT STDMETHODCALLTYPE DeviceState(IDirectInputDevice8W* self, DWORD bytes, LPVOID output) noexcept {
    const auto original = DeviceOriginal<InputStateFn>(self, 9, state_hooks);
    if (!original) return DIERR_GENERIC;
    const HRESULT result = original(self, bytes, output);
    const DWORD error = GetLastError();
#if MGS_BACKGROUND_WORKER
    if (worker_scenario::Requested()) {
        if (FAILED(result)) {
            worker_scenario::InputFailure("GetDeviceState_original_failure", result, true);
            SetLastError(error); return result;
        }
        DWORD type = 0; const bool typed = WorkerTyped(self, bytes, type);
        if (typed && type == DI8DEVTYPE_MOUSE) CountWorker(WorkerCounter::di_mouse_state_calls);
        const bool admitted = worker_scenario::ThreadAdmitted();
        if (output && bytes <= 4096) std::memset(output, 0, bytes);
        bool supplied = false;
        if (output && worker_scenario::MayReplaceState(result, typed, admitted))
            supplied = type == DI8DEVTYPE_KEYBOARD ? worker_scenario::Keyboard(output, bytes) : worker_scenario::Mouse(output, bytes);
        if (supplied && type == DI8DEVTYPE_MOUSE) {
            CountWorker(WorkerCounter::di_mouse_state_supplied);
            DIMOUSESTATE state{}; std::memcpy(&state, output, sizeof(state));
            if (state.lX || state.lY) CountWorker(WorkerCounter::di_mouse_state_axes);
            if (state.rgbButtons[0] || state.rgbButtons[1]) CountWorker(WorkerCounter::di_mouse_state_buttons);
        }
        if (!supplied) worker_scenario::InputFailure("GetDeviceState_worker_refusal", DIERR_UNSUPPORTED, typed);
        SetLastError(error);
        return SUCCEEDED(result) && !supplied ? DIERR_UNSUPPORTED : result;
    }
#endif
    if (SUCCEEDED(result) && output && CapturingInput() && KeyboardOrMouse(self) &&
        (bytes == 256 || bytes == sizeof(DIMOUSESTATE) || bytes == sizeof(DIMOUSESTATE2)))
        std::memset(output, 0, bytes);
    SetLastError(error);
    return result;
}
HRESULT STDMETHODCALLTYPE DeviceData(IDirectInputDevice8W* self, DWORD bytes, LPDIDEVICEOBJECTDATA output, LPDWORD count, DWORD flags) noexcept {
    const auto original = DeviceOriginal<InputDataFn>(self, 10, data_hooks);
    if (!original) return DIERR_GENERIC;
#if MGS_BACKGROUND_WORKER
    if (worker_scenario::Requested()) {
        const DWORD capacity = count ? *count : 0;
        const HRESULT result = original(self, bytes, output, count, flags);
        const DWORD error = GetLastError();
        if (FAILED(result)) {
            worker_scenario::InputFailure("GetDeviceData_original_failure", result, true);
            SetLastError(error); return result;
        }
        DWORD type = 0; bool typed = WorkerTyped(self, 256, type);
        if (!typed) typed = WorkerTyped(self, sizeof(DIMOUSESTATE), type) || WorkerTyped(self, sizeof(DIMOUSESTATE2), type);
        if (typed && type == DI8DEVTYPE_MOUSE) CountWorker(WorkerCounter::di_mouse_buffer_calls);
        const bool valid = typed && worker_scenario::ThreadAdmitted() && count && bytes == sizeof(DIDEVICEOBJECTDATA) && !(flags & ~DIGDD_PEEK) && (capacity <= 64 || (!output && capacity == INFINITE));
        if (output && capacity <= 64 && bytes == sizeof(DIDEVICEOBJECTDATA)) std::memset(output, 0, static_cast<std::size_t>(capacity) * bytes);
        if (count) *count = 0;
        if (SUCCEEDED(result) && valid) {
            worker_scenario::Event events[64]{};
            const auto amount = worker_scenario::Buffered(type == DI8DEVTYPE_KEYBOARD ? worker_scenario::Device::keyboard : worker_scenario::Device::mouse,
                events, capacity == INFINITE ? 64 : capacity, (flags & DIGDD_PEEK) != 0);
            if (type == DI8DEVTYPE_MOUSE) CountWorker((flags & DIGDD_PEEK) ? WorkerCounter::di_mouse_buffer_peek_events : WorkerCounter::di_mouse_buffer_events, amount);
            if (output) for (unsigned i = 0; i < amount; ++i) output[i] = {events[i].offset, events[i].value, events[i].timestamp, events[i].sequence, 0};
            *count = amount;
        } else worker_scenario::InputFailure("GetDeviceData_worker_refusal", DIERR_UNSUPPORTED, typed);
        SetLastError(error); return SUCCEEDED(result) && !valid ? DIERR_UNSUPPORTED : result;
    }
#endif
    const bool capture = CapturingInput() && KeyboardOrMouse(self);
    const HRESULT result = original(self, bytes, output, count, capture ? flags & ~DIGDD_PEEK : flags);
    const DWORD error = GetLastError();
    if (SUCCEEDED(result) && capture && count) {
        // Also discard events beyond the requested batch, including peek polls.
        DWORD discard = INFINITE;
        original(self, bytes, nullptr, &discard, 0);
        *count = 0;
    }
    SetLastError(error);
    return result;
}
BOOL WINAPI CursorPos(int x, int y) noexcept {
    const DWORD error = GetLastError();
    if (worker_scenario::Requested()) { SetLastError(error); return TRUE; }
    if (CapturingInput()) { SetLastError(error); return TRUE; }
    SetLastError(error);
    return reinterpret_cast<CursorPosFn>(cursor_hook.original)(x, y);
}
BOOL WINAPI Clip(const RECT* rect) noexcept {
    const DWORD error = GetLastError();
    if (worker_scenario::Requested()) { SetLastError(error); return TRUE; }
    if (CapturingInput()) {
        AcquireSRWLockExclusive(&clip_lock);
        clip_saved = true; restore_unclipped = rect == nullptr;
        if (rect) previous_clip = *rect;
        ReleaseSRWLockExclusive(&clip_lock);
        SetLastError(error); return TRUE;
    }
    SetLastError(error);
    return reinterpret_cast<ClipFn>(clip_hook.original)(rect);
}
BOOL WINAPI Keyboard(PBYTE keys) noexcept {
    const BOOL result = reinterpret_cast<KeyboardFn>(keyboard_hook.original)(keys);
    const DWORD error = GetLastError();
    if (worker_scenario::Requested()) {
        if (keys) {
            std::memset(keys, 0, 256);
#if MGS_BACKGROUND_WORKER
            std::uint8_t scans[256]{};
            if (worker_scenario::Keyboard(scans, sizeof(scans)))
                for (UINT scan = 0; scan < 256; ++scan) if (scans[scan] & 0x80) {
                    const UINT vk = MapVirtualKeyW((scan & 0x7F) | ((scan & 0x80) ? 0xE000 : 0), MAPVK_VSC_TO_VK_EX);
                    if (vk && vk < 256) keys[vk] = 0x80;
                    if (vk == VK_LSHIFT || vk == VK_RSHIFT) keys[VK_SHIFT] = 0x80;
                    if (vk == VK_LCONTROL || vk == VK_RCONTROL) keys[VK_CONTROL] = 0x80;
                }
#endif
        }
        SetLastError(error); return result;
    }
    if (result && keys && CapturingInput()) std::memset(keys, 0, 256);
    SetLastError(error);
    return result;
}
PadFn PadOriginal() noexcept {
#ifdef MGS_DEBUG_OVERLAY_TESTING
    if (fixture_pad.original) return reinterpret_cast<PadFn>(fixture_pad.original);
#endif
    return reinterpret_cast<PadFn>(pad_hook.original);
}
bool PadActive(const XINPUT_GAMEPAD& g) noexcept {
    return g.wButtons || g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD || g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        g.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || g.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
        g.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || g.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
        g.sThumbRX > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || g.sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ||
        g.sThumbRY > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || g.sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
}
bool PadsReleased() noexcept {
    const auto original = PadOriginal();
    if (!original) return true;
    const DWORD error = GetLastError();
    bool released = true;
    // Query the original export, never our detour. This also handles controllers
    // that were not polled while open and disconnects during the release guard.
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state{};
        if (original(i, &state) != ERROR_SUCCESS) continue;
        const auto& g = state.Gamepad;
        if (g.wButtons || g.bLeftTrigger || g.bRightTrigger || PadActive(g)) released = false;
    }
    SetLastError(error);
    return released;
}
DWORD WINAPI Pad(DWORD index, XINPUT_STATE* state) noexcept {
    if (worker_scenario::Requested()) {
        const DWORD error=GetLastError();
        if(state) *state={};
        const bool supplied=WorkerVirtualPadState(index,state,sizeof(XINPUT_STATE));
        SetLastError(error);return supplied?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
    }
    const DWORD result = PadOriginal()(index, state);
    const DWORD error = GetLastError();
    if (result == ERROR_SUCCESS && state && PadActive(state->Gamepad)) last_pad_ms.store(GetTickCount64(), std::memory_order_relaxed);
    if (result == ERROR_SUCCESS && state && CapturingInput()) std::memset(&state->Gamepad, 0, sizeof(state->Gamepad));
    SetLastError(error);
    return result;
}
bool InputHooks() noexcept {
    wchar_t input_path[MAX_PATH]{};
    if (!GetSystemDirectoryW(input_path, ARRAYSIZE(input_path)) ||
        FAILED(StringCchCatW(input_path, ARRAYSIZE(input_path), L"\\dinput8.dll"))) {
        Fail("input", "system_directory", FailKind::win32, static_cast<long>(GetLastError())); return false;
    }
    const HMODULE input_module = LoadLibraryExW(input_path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!input_module) { Fail("input", "dinput8_load", FailKind::win32, static_cast<long>(GetLastError())); return false; }
    const auto create = reinterpret_cast<InputCreateFn>(GetProcAddress(input_module, "DirectInput8Create"));
    if (!create) { Fail("input", "DirectInput8Create_export", FailKind::win32, static_cast<long>(GetLastError())); return false; }
    IDirectInput8W* input = nullptr;
    const HRESULT made_input = create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&input), nullptr);
    if (FAILED(made_input) || !input) { Fail("input", "DirectInput8Create", FailKind::hr, made_input); return false; }
    const GUID* identifiers[] = {&GUID_SysKeyboard, &GUID_SysMouse};
    const char* const device_names[] = {"CreateDevice_keyboard", "CreateDevice_mouse"};
    const char* const state_names[] = {"keyboard_GetDeviceState", "mouse_GetDeviceState"};
    const char* const data_names[] = {"keyboard_GetDeviceData", "mouse_GetDeviceData"};
#if MGS_BACKGROUND_WORKER
    const char* const format_names[] = {"keyboard_SetDataFormat", "mouse_SetDataFormat"};
    const char* const coop_names[] = {"keyboard_SetCooperativeLevel", "mouse_SetCooperativeLevel"};
#endif
    bool ok = true;
    for (unsigned i = 0; i < ARRAYSIZE(identifiers) && ok; ++i) {
        IDirectInputDevice8W* object = nullptr;
        const HRESULT made_device = input->CreateDevice(*identifiers[i], &object, nullptr);
        if (FAILED(made_device) || !object) { Fail("input", device_names[i], FailKind::hr, made_device); ok = false; break; }
        void** methods = *reinterpret_cast<void***>(object);
        if (i == 0 || methods[9] != state_hooks[0].target)
            ok = StepHook("input", state_names[i], state_hooks[i], methods[9], reinterpret_cast<void*>(&DeviceState));
        if (ok && (i == 0 || methods[10] != data_hooks[0].target))
            ok = StepHook("input", data_names[i], data_hooks[i], methods[10], reinterpret_cast<void*>(&DeviceData));
#if MGS_BACKGROUND_WORKER
        if (ok && worker_scenario::Requested() && (i == 0 || methods[11] != format_hooks[0].target))
            ok = StepHook("input", format_names[i], format_hooks[i], methods[11], reinterpret_cast<void*>(&WorkerFormat));
        if (ok && worker_scenario::Requested() && (i == 0 || methods[13] != coop_hooks[0].target))
            ok = StepHook("input", coop_names[i], coop_hooks[i], methods[13], reinterpret_cast<void*>(&WorkerCoop));
#endif
        object->Release();
    }
    input->Release();
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    if (ok) ok = StepHook("input", "SetCursorPos", cursor_hook, reinterpret_cast<void*>(GetProcAddress(user, "SetCursorPos")), reinterpret_cast<void*>(&CursorPos));
    if (ok) ok = StepHook("input", "ClipCursor", clip_hook, reinterpret_cast<void*>(GetProcAddress(user, "ClipCursor")), reinterpret_cast<void*>(&Clip));
    if (ok) ok = StepHook("input", "GetKeyboardState", keyboard_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetKeyboardState")), reinterpret_cast<void*>(&Keyboard));
    const HMODULE xinput = GetModuleHandleW(L"XINPUT1_3.dll");
    if (ok && xinput) ok = StepHook("input", "XInputGetState", pad_hook, reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetState")), reinterpret_cast<void*>(&Pad));
#if MGS_BACKGROUND_WORKER
    if (ok && worker_scenario::Requested()) {
        if (!xinput || !pad_hook.target) { Fail("input", "xinput1_3_module", FailKind::null_address, 0); ok = false; }
        else ok =
            StepHook("input", "GetAsyncKeyState", async_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetAsyncKeyState")), reinterpret_cast<void*>(&WorkerAsync)) &&
            StepHook("input", "GetKeyState", key_state_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetKeyState")), reinterpret_cast<void*>(&WorkerKeyState)) &&
            StepHook("input", "GetRawInputData", raw_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetRawInputData")), reinterpret_cast<void*>(&WorkerRawInput)) &&
            StepHook("input", "GetRawInputBuffer", raw_buffer_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetRawInputBuffer")), reinterpret_cast<void*>(&WorkerRawBuffer)) &&
            StepHook("input", "GetForegroundWindow", foreground_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetForegroundWindow")), reinterpret_cast<void*>(&WorkerForeground)) &&
            StepHook("input", "GetCursorPos", get_cursor_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetCursorPos")), reinterpret_cast<void*>(&WorkerGetCursorPos));
    }
#endif
    return ok;
}

template<class T> void Release(T*& value) noexcept { if (value) value->Release(); value = nullptr; }
void ReleaseSleepFilter() noexcept;
bool CreateTarget(IDXGISwapChain* chain) noexcept {
    ID3D11Texture2D* buffer = nullptr;
    if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer)))) return false;
    const HRESULT result = device->CreateRenderTargetView(buffer, nullptr, &target);
    buffer->Release();
    return SUCCEEDED(result);
}
void Cleanup() noexcept {
    ChangeOpen(false);
    const HWND hwnd = window.exchange(nullptr, std::memory_order_acq_rel);
    if (hwnd && IsWindow(hwnd) && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)) == &WindowProc)
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_wndproc.load(std::memory_order_acquire)));
    RestoreClip(hwnd); was_open = false;
    if (gui) {
        ImGui::SetCurrentContext(gui);
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX11_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(gui);
    }
    gui = nullptr; initialized = false; swapchain = nullptr;
    ReleaseSleepFilter(); Release(target); Release(context); Release(device);
    AcquireSRWLockExclusive(&message_lock); message_count = 0; ReleaseSRWLockExclusive(&message_lock);
}
// The rescue prompt's face: Windows' DIN-style Bahnschrift (closest to the
// game's HUD lettering), else Segoe UI; null keeps the menu font.
ImFont* hud_font = nullptr;
void LoadHudFont(ImGuiIO& io) noexcept {
    hud_font = nullptr;
    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    if (!length || length >= ARRAYSIZE(windows)) return;
    static const wchar_t* const files[] = {L"\\Fonts\\bahnschrift.ttf", L"\\Fonts\\seguisb.ttf", L"\\Fonts\\segoeui.ttf"};
    for (const wchar_t* file : files) {
        wchar_t path[MAX_PATH]{};
        char utf8[MAX_PATH * 3]{};
        if (FAILED(StringCchCopyW(path, ARRAYSIZE(path), windows)) || FAILED(StringCchCatW(path, ARRAYSIZE(path), file))) continue;
        const DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr)) continue;
        ImFontConfig config{};
        config.SizePixels = 22.0f;
        config.Flags |= ImFontFlags_NoLoadError;
        hud_font = io.Fonts->AddFontFromFileTTF(utf8, 22.0f, &config);
        if (hud_font) return;
    }
}
// Readable defaults shared by the interactive and worker-drawn menus.
void Style(ImGuiIO& io) noexcept {
    ImFontConfig font{}; font.SizePixels = 18.0f; io.Fonts->AddFontDefault(&font);
    LoadHudFont(io);
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f; style.FrameRounding = 4.0f;
    style.WindowPadding = ImVec2(14, 12); style.ItemSpacing = ImVec2(10, 8); style.FramePadding = ImVec2(8, 5);
}
bool Initialize(IDXGISwapChain* chain) noexcept {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow || !IsWindow(desc.OutputWindow)) return false;
    DWORD owner = 0; GetWindowThreadProcessId(desc.OutputWindow, &owner);
    if (owner != GetCurrentProcessId() || FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) return false;
    if (FAILED(device->GetDeviceRemovedReason())) { Release(device); return false; }
    device->GetImmediateContext(&context);
    if (!context || !CreateTarget(chain)) { Cleanup(); return false; }
    gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    Style(io);
    if (!ImGui_ImplWin32_Init(desc.OutputWindow) || !ImGui_ImplDX11_Init(device, context)) { Cleanup(); return false; }
    SetLastError(ERROR_SUCCESS);
    original_wndproc.store(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC)), std::memory_order_release);
    const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
    if (!previous && GetLastError() != ERROR_SUCCESS) { Cleanup(); return false; }
    original_wndproc.store(previous, std::memory_order_release);
    window.store(desc.OutputWindow, std::memory_order_release);
    swapchain = chain; initialized = true;
    Log("overlay=render_ready; api=dx11; menu=closed; input=keyboard_mouse_pad\r\n");
    return true;
}

// The upstream renderer restores its touched IA/VS/PS/GS/raster/depth/blend
// state. It clears HS/DS/CS without restoring them; preserve those and the
// application's complete output-merger target state around our draw too.
struct ExtraState {
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth = nullptr;
    ID3D11UnorderedAccessView* unordered[D3D11_PS_CS_UAV_REGISTER_COUNT]{};
    ID3D11UnorderedAccessView* compute_views[D3D11_PS_CS_UAV_REGISTER_COUNT]{};
    ID3D11ShaderResourceView* resources[6][D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    UINT target_count = 0;
    ID3D11HullShader* hull = nullptr;
    ID3D11DomainShader* domain = nullptr;
    ID3D11ComputeShader* compute = nullptr;
    ID3D11ClassInstance* hull_classes[256]{}, *domain_classes[256]{}, *compute_classes[256]{};
    UINT hull_count = 256, domain_count = 256, compute_count = 256;
    explicit ExtraState(ID3D11DeviceContext* ctx) noexcept {
        ctx->OMGetRenderTargets(ARRAYSIZE(targets), targets, &depth);
        for (UINT i = 0; i < ARRAYSIZE(targets); ++i) if (targets[i]) target_count = i + 1;
        if (target_count < ARRAYSIZE(unordered))
            ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, target_count,
                static_cast<UINT>(ARRAYSIZE(unordered)) - target_count, unordered + target_count);
        // Binding the backbuffer RTV can implicitly unbind shader inputs/UAVs
        // outside the slots changed by ImGui. Save those before the bind too.
        constexpr UINT slots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        ctx->VSGetShaderResources(0, slots, resources[0]); ctx->PSGetShaderResources(0, slots, resources[1]);
        ctx->GSGetShaderResources(0, slots, resources[2]); ctx->HSGetShaderResources(0, slots, resources[3]);
        ctx->DSGetShaderResources(0, slots, resources[4]); ctx->CSGetShaderResources(0, slots, resources[5]);
        ctx->CSGetUnorderedAccessViews(0, ARRAYSIZE(compute_views), compute_views);
        ctx->HSGetShader(&hull, hull_classes, &hull_count);
        ctx->DSGetShader(&domain, domain_classes, &domain_count);
        ctx->CSGetShader(&compute, compute_classes, &compute_count);
    }
    void Restore(ID3D11DeviceContext* ctx) noexcept {
        UINT counters[D3D11_PS_CS_UAV_REGISTER_COUNT];
        for (auto& item : counters) item = static_cast<UINT>(-1);
        ctx->OMSetRenderTargetsAndUnorderedAccessViews(target_count, targets, depth, target_count,
            static_cast<UINT>(ARRAYSIZE(unordered)) - target_count, unordered + target_count, counters);
        ctx->HSSetShader(hull, hull_classes, hull_count);
        ctx->DSSetShader(domain, domain_classes, domain_count);
        ctx->CSSetShader(compute, compute_classes, compute_count);
        constexpr UINT slots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        ctx->VSSetShaderResources(0, slots, resources[0]); ctx->PSSetShaderResources(0, slots, resources[1]);
        ctx->GSSetShaderResources(0, slots, resources[2]); ctx->HSSetShaderResources(0, slots, resources[3]);
        ctx->DSSetShaderResources(0, slots, resources[4]); ctx->CSSetShaderResources(0, slots, resources[5]);
        ctx->CSSetUnorderedAccessViews(0, ARRAYSIZE(compute_views), compute_views, counters);
        for (auto& item : targets) Release(item);
        for (auto& item : unordered) Release(item);
        for (auto& item : compute_views) Release(item);
        for (auto& stage : resources) for (auto& item : stage) Release(item);
        Release(depth); Release(hull); Release(domain); Release(compute);
        for (UINT i = 0; i < hull_count; ++i) Release(hull_classes[i]);
        for (UINT i = 0; i < domain_count; ++i) Release(domain_classes[i]);
        for (UINT i = 0; i < compute_count; ++i) Release(compute_classes[i]);
    }
};
// Partner name tag layer. NametagFrame classifies this Present's newest frame
// (records stale/hidden/none itself and returns false); DrawNametagLayer draws
// a returned frame on the background list and records it. Display units are
// io.DisplaySize (backbuffer pixels while the menu is closed).
bool NametagFrame(nametag::Frame& frame, nametag::RenderNote& note, UINT bb_w, UINT bb_h) noexcept {
    note = {};
    if (!nametag::Enabled()) return false;
    note.thread = GetCurrentThreadId();
    note.bb[0] = bb_w; note.bb[1] = bb_h;
    if (!nametag::Take(frame)) { note.kind = nametag::Drawn::none; nametag::NoteRender(note); return false; }
    // The clock is read after the take: a frame published in between is never
    // "from the future" (GetTickCount64 is monotonic; a tick boundary there
    // would otherwise drop one fresh frame as stale).
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG age = now >= frame.tick_ms ? now - frame.tick_ms : 0;
    note.age_ms = age > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(age);
    if (age > nametag::kStaleMs) { note.kind = nametag::Drawn::stale; nametag::NoteRender(note); return false; }
    if (!frame.visible) { note.kind = nametag::Drawn::hidden; nametag::NoteRender(note); return false; }
    return true;
}
void DrawNametagLayer(const nametag::Frame& frame, nametag::RenderNote& note, const ImVec2& display) noexcept {
    note.display[0] = static_cast<std::uint32_t>(display.x); note.display[1] = static_cast<std::uint32_t>(display.y);
    note.kind = nametag::Drawn::hidden;
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    if (!list || !(display.x > 0.0f) || !(display.y > 0.0f)) { nametag::NoteRender(note); return; }
    const float scale = std::fmin(2.0f, std::fmax(0.8f, display.y / 1080.0f));
    if (frame.visible) {
        float x = 0, y = 0;
        nametag::NdcToDisplay(frame.rect, frame.ndc, display.x, display.y, x, y);
        const float alpha = std::fmin(1.0f, std::fmax(0.0f, frame.alpha));
        const auto a = static_cast<int>(alpha * 255.0f + 0.5f);
        const ImU32 text = frame.hover ? IM_COL32(170, 255, 185, a) : IM_COL32(255, 255, 255, a);
        const ImU32 shadow = IM_COL32(0, 0, 0, a * 7 / 8), plate = IM_COL32(0, 0, 0, a * 3 / 8);
        ImFont* font = hud_font ? hud_font : ImGui::GetFont();
        // The bitmap menu font stays crisp only at whole multiples of 18 px; the HUD face scales freely.
        const float size = hud_font ? std::floor(19.0f * scale + 0.5f) : font->LegacySize * std::fmax(1.0f, std::floor(scale + 0.25f));
        const float tri = 6.0f * scale, cap = std::fmax(160.0f * scale, display.x * 0.3f);
        // The name is sanitized UTF-8 (at most 24 Latin-1 code points); cut whole code points with "...".
        char label[nametag::kNameBytes + 4]{};
        std::memcpy(label, frame.name, nametag::kNameBytes);
        ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        std::size_t length = std::strlen(label);
        while (ts.x > cap && length > 1) {
            do { --length; } while (length && (static_cast<unsigned char>(label[length]) & 0xC0) == 0x80);
            std::memcpy(label + length, "...", 4);
            ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, label);
        }
        const float bx = std::floor(x - ts.x * 0.5f), by = std::floor(y - tri * 1.4f - 3.0f * scale - ts.y);
        list->AddRectFilled(ImVec2(bx - 4.0f * scale, by - 2.0f * scale), ImVec2(bx + ts.x + 4.0f * scale, by + ts.y + 2.0f * scale), plate, 3.0f * scale);
        const ImVec2 offsets[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto& o : offsets) list->AddText(font, size, ImVec2(bx + o.x, by + o.y), shadow, label);
        list->AddText(font, size, ImVec2(bx, by), text, label);
        list->AddTriangleFilled(ImVec2(x - tri, y - tri * 1.4f), ImVec2(x + tri, y - tri * 1.4f), ImVec2(x, y), text);
        note.kind = nametag::Drawn::drawn;
        note.px[0] = x; note.px[1] = y;
        note.label_box[0] = bx; note.label_box[1] = by; note.label_box[2] = bx + ts.x; note.label_box[3] = by + ts.y;
        note.alpha = alpha;
    }
    // Preserve the game reticle. Capsule hover is not an occlusion-tested native
    // target, so it cannot authorize drawing or recoloring a crosshair.
    nametag::NoteRender(note);
}
// The in-game co-op status line and toasts (coop_hud; docs/two-pc-play.md
// "In game"): copied link data and events only, drawn top-right on the
// background list while the F9 menu is closed (its Online panel says the
// same), hidden while a demo plays. The model is Present-thread only.
coop_hud::Model hud_model;
struct HudFrame {
    coop_hud::Line line;
    bool show_line;
    coop_hud::Line downed_line;
    bool show_downed, downed_progress;
    float revive_progress;
    coop_hud::RescuePrompt rescue;
    bool show_rescue;
    nametag::Frame tag;  // the teammate's projected name tag, when fresh
    bool tag_fresh;
    bool pad_glyphs;     // the controller was used after the keyboard/mouse
    const coop_hud::Toast* toasts[coop_hud::kMaxToasts];
    unsigned toast_count;
};
bool HudPrepare(HudFrame& f, bool menu_open) noexcept {
    f = HudFrame{};
    const bool statusLine = coop_hud::Enabled();
    if (!statusLine && !coop_hud::RescueEnabled()) return false;
    const ULONGLONG now = GetTickCount64();
    coop_hud::LinkView view{};
    coop_hud::GetLink(view);
    // The partner's name: the Steam persona (online), else the name tag's
    // received name; masked id otherwise (FormatLine).
    char name[48]{};
    const steam_state::OnlineSnapshot online = steam_state::GetOnline();
    const nametag::Telemetry tag = nametag::GetTelemetry();
    if (online.peer_name[0]) StringCchCopyA(name, ARRAYSIZE(name), online.peer_name);
    else if (tag.name_src == nametag::NameSource::remote && tag.name[0]) StringCchCopyA(name, ARRAYSIZE(name), tag.name);
    std::uint32_t new_toasts = 0;
    coop_hud::Event events[8]{};
    const unsigned taken = statusLine ? coop_hud::Take(events, 8, hud_model.cursor()) : 0;
    for (unsigned i = 0; i < taken; ++i) {
        char text[coop_hud::kToastChars]{};
        coop_hud::Tone tone{};
        if (!coop_hud::FormatToast(events[i], view, name, text, sizeof(text), tone)) continue;
        hud_model.Add(text, tone, now);
        ++new_toasts;
    }
    // A demo hides it (the HUD bit of the Lua tick, fresh only while the game
    // runs; the title and loading screens show it).
    const bool demo = (tag.hud & nametag::kHudDemo) != 0 && now >= tag.tick_ms && now - tag.tick_ms < 1000;
    if (menu_open || demo) {
        coop_hud::NoteFrame(nullptr, nullptr, true, taken, new_toasts, now);
        return false;
    }
    coop_hud::PartnerView partner{};
    coop_hud::GetPartner(partner); // copied owner publication only
    f.show_line = statusLine && coop_hud::FormatLine(view, name, coop_hud::GetMode(), now, f.line, &partner);
    coop_hud::DownedView downed{};
    coop_hud::GetDowned(downed); // copied receipt facts; never local game reads
    // A fresh downed receipt can arrive before the link line has a mission.
    // A published link mission still has to match; another scene stays hidden.
    const auto downedMission = view.mission ? view.mission : downed.mission;
    f.show_downed = coop_hud::FormatDowned(downed, downedMission, now, f.downed_line, f.revive_progress);
    f.downed_progress = f.show_downed && downed.teammateDown && downed.inRange && !downed.localDown && f.revive_progress > 0.0f;
    f.show_rescue = coop_hud::ClassifyDowned(downed, downedMission, now, f.rescue) && f.rescue.state != coop_hud::Rescue::carrying &&
        f.rescue.state != coop_hud::Rescue::transfer;
    if (f.show_rescue && nametag::Take(f.tag)) {
        const ULONGLONG tagNow = GetTickCount64();
        f.tag_fresh = tagNow >= f.tag.tick_ms && tagNow - f.tag.tick_ms <= nametag::kStaleMs;
    }
    f.pad_glyphs = last_pad_ms.load(std::memory_order_relaxed) > last_keyboard_ms.load(std::memory_order_relaxed);
    f.toast_count = statusLine ? hud_model.Active(now, f.toasts, coop_hud::kMaxToasts) : 0;
    if (statusLine)
        coop_hud::NoteFrame(f.show_line ? &f.line : nullptr, f.toast_count ? f.toasts[0]->text : nullptr, false, taken, new_toasts, now);
    return f.show_line || f.show_rescue || f.toast_count;
}
ImU32 HudColor(coop_hud::Tone tone, int alpha) noexcept {
    switch (tone) {
    case coop_hud::Tone::good: return IM_COL32(120, 255, 150, alpha);
    case coop_hud::Tone::warn: return IM_COL32(255, 214, 100, alpha);
    case coop_hud::Tone::bad: return IM_COL32(255, 120, 110, alpha);
    default: return IM_COL32(240, 240, 240, alpha);
    }
}
// Outlined HUD text (the game's prompts are white with a dark edge).
void HudText(ImDrawList* list, ImFont* font, float size, ImVec2 at, ImU32 color, const char* text) noexcept {
    const int alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 0xFF);
    const ImU32 edge = IM_COL32(0, 0, 0, alpha * 3 / 4);
    const ImVec2 offsets[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}};
    for (const auto& o : offsets) list->AddText(font, size, ImVec2(at.x + o.x, at.y + o.y), edge, text);
    list->AddText(font, size, at, color, text);
}
// The Action button as the game would show it for this copy's last input:
// the E key cap for keyboard/mouse, the yellow Y face button for a controller.
// Returns the glyph radius used.
float DrawActionGlyph(ImDrawList* list, ImFont* font, ImVec2 c, float r, bool pad, int alpha) noexcept {
    const char* letter = pad ? "Y" : "E";
    const float letterSize = r * 1.25f;
    const ImVec2 ts = font->CalcTextSizeA(letterSize, FLT_MAX, 0.0f, letter);
    if (pad) {
        list->AddCircleFilled(c, r, IM_COL32(236, 190, 40, alpha), 32);
        list->AddCircle(c, r, IM_COL32(20, 20, 20, alpha * 3 / 4), 32, 1.5f);
        list->AddText(font, letterSize, ImVec2(std::floor(c.x - ts.x * 0.5f), std::floor(c.y - ts.y * 0.5f)), IM_COL32(20, 20, 20, alpha), letter);
    } else {
        const float h = r * 0.82f;
        list->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), IM_COL32(16, 18, 20, alpha * 3 / 4), r * 0.22f);
        list->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), IM_COL32(240, 240, 240, alpha), r * 0.22f, 0, std::fmax(1.5f, r * 0.1f));
        list->AddText(font, letterSize, ImVec2(std::floor(c.x - ts.x * 0.5f), std::floor(c.y - ts.y * 0.5f)), IM_COL32(245, 245, 245, alpha), letter);
    }
    return r;
}
// The sleeper's view: the finished game frame redrawn as dark greyscale with
// a soft vignette, from an ImGui draw callback so the backend restores its
// state and the HUD that follows stays in colour.
ID3D11Texture2D* sleep_copy = nullptr;
ID3D11ShaderResourceView* sleep_view = nullptr;
ID3D11VertexShader* sleep_vs = nullptr;
ID3D11PixelShader* sleep_ps[2]{};
bool sleep_refused = false, sleep_logged = false;
// An sRGB frame reads and writes linear light; the SRGB variant darkens in
// display space so the result looks equally dark on either format.
constexpr char kSleepShader[] = R"(
Texture2D frame : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
float4 ps(float4 pos : SV_Position) : SV_Target {
    uint w, h; frame.GetDimensions(w, h);
    float3 c = frame.Load(int3(pos.xy, 0)).rgb;
#if SRGB
    c = pow(c, 1.0 / 2.2);
#endif
    float grey = dot(c, float3(0.299, 0.587, 0.114));
    float2 d = pos.xy / float2(w, h) - 0.5;
    float edge = saturate(1.0 - dot(d, d) * 1.6);
    float v = grey * 0.5 * (0.55 + 0.45 * edge);
#if SRGB
    v = pow(v, 2.2);
#endif
    return float4(v.xxx, 1.0);
}
)";
bool SrgbFormat(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}
void ReleaseSleepCopy() noexcept { Release(sleep_view); Release(sleep_copy); }
void ReleaseSleepFilter() noexcept {
    ReleaseSleepCopy(); Release(sleep_vs); Release(sleep_ps[0]); Release(sleep_ps[1]); sleep_refused = sleep_logged = false;
}
bool SleepFilterReady() noexcept {
    if (sleep_refused || !device) return false;
    if (sleep_vs && sleep_ps[0] && sleep_ps[1]) return true;
    const D3D_SHADER_MACRO linear[] = {{"SRGB", "0"}, {nullptr, nullptr}}, srgb[] = {{"SRGB", "1"}, {nullptr, nullptr}};
    ID3DBlob* vs = nullptr; ID3DBlob* ps[2]{};
    const bool ok =
        SUCCEEDED(D3DCompile(kSleepShader, sizeof(kSleepShader) - 1, "sleep_view", linear, nullptr, "vs", "vs_4_0", 0, 0, &vs, nullptr)) &&
        SUCCEEDED(D3DCompile(kSleepShader, sizeof(kSleepShader) - 1, "sleep_view", linear, nullptr, "ps", "ps_4_0", 0, 0, &ps[0], nullptr)) &&
        SUCCEEDED(D3DCompile(kSleepShader, sizeof(kSleepShader) - 1, "sleep_view", srgb, nullptr, "ps", "ps_4_0", 0, 0, &ps[1], nullptr)) &&
        SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &sleep_vs)) &&
        SUCCEEDED(device->CreatePixelShader(ps[0]->GetBufferPointer(), ps[0]->GetBufferSize(), nullptr, &sleep_ps[0])) &&
        SUCCEEDED(device->CreatePixelShader(ps[1]->GetBufferPointer(), ps[1]->GetBufferSize(), nullptr, &sleep_ps[1]));
    Release(vs); Release(ps[0]); Release(ps[1]);
    if (!ok) {
        Release(sleep_vs); Release(sleep_ps[0]); Release(sleep_ps[1]); sleep_refused = true;
        Log("overlay=sleep_view_refused; fallback=dark_wash\r\n");
    }
    return ok;
}
void SleepFilter(const ImDrawList*, const ImDrawCmd*) noexcept {
    if (!target || !context || !sleep_vs || !sleep_ps[0] || !sleep_ps[1]) return;
    ID3D11Resource* resource = nullptr; ID3D11Texture2D* back = nullptr;
    target->GetResource(&resource);
    if (!resource || FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) { Release(resource); return; }
    Release(resource);
    D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
    if (sleep_copy) {
        D3D11_TEXTURE2D_DESC have{}; sleep_copy->GetDesc(&have);
        if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format) ReleaseSleepCopy();
    }
    if (!sleep_copy) {
        D3D11_TEXTURE2D_DESC copy = desc;
        copy.MipLevels = copy.ArraySize = 1; copy.SampleDesc = {1, 0};
        copy.Usage = D3D11_USAGE_DEFAULT; copy.BindFlags = D3D11_BIND_SHADER_RESOURCE; copy.CPUAccessFlags = copy.MiscFlags = 0;
        if (FAILED(device->CreateTexture2D(&copy, nullptr, &sleep_copy)) || FAILED(device->CreateShaderResourceView(sleep_copy, nullptr, &sleep_view))) {
            ReleaseSleepCopy(); sleep_refused = true; back->Release();
            Log("overlay=sleep_view_refused; reason=frame_copy; fallback=dark_wash\r\n");
            return;
        }
    }
    if (desc.SampleDesc.Count > 1) context->ResolveSubresource(sleep_copy, 0, back, 0, desc.Format);
    else context->CopyResource(sleep_copy, back);
    back->Release();
    const bool srgb = SrgbFormat(desc.Format);
    if (!sleep_logged) {
        sleep_logged = true;
        char line[128]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "overlay=sleep_view; format=%u; srgb=%u; samples=%u\r\n",
            static_cast<unsigned>(desc.Format), srgb ? 1u : 0u, desc.SampleDesc.Count))) Log(line);
    }
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0.0f, 1.0f};
    const D3D11_RECT all{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    context->RSSetViewports(1, &viewport); context->RSSetScissorRects(1, &all);
    context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(sleep_vs, nullptr, 0); context->PSSetShader(sleep_ps[srgb ? 1 : 0], nullptr, 0);
    context->PSSetShaderResources(0, 1, &sleep_view);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    context->PSSetShaderResources(0, 1, &none);
}
// Queued first in the frame, so the name tag and HUD draw over it in colour.
void DrawSleeperView(const HudFrame& f, const ImVec2& display) noexcept {
    using coop_hud::Rescue;
    const auto& p = f.rescue;
    if (!f.show_rescue || !p.sleeping || (p.state != Rescue::self_asleep && p.state != Rescue::carried)) return;
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    if (SleepFilterReady()) {
        list->AddCallback(&SleepFilter, nullptr);
        list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    } else {
        list->AddRectFilled(ImVec2(0, 0), display, IM_COL32(8, 8, 10, 170));
    }
}
// The rescue prompt: anchored under the teammate's name tag while it is on
// screen, else lower center. A status pill with the distance when far, the
// Action button with a radial hold ring when in range, and a short status
// for the player who is down. Pickup keeps the stock carry icon.
void DrawRescuePrompt(const HudFrame& f, ImDrawList* list, const ImVec2& display, float scale) noexcept {
    using coop_hud::Rescue;
    ImFont* font = hud_font ? hud_font : ImGui::GetFont();
    const auto& p = f.rescue;
    const ImU32 status = p.sleeping ? IM_COL32(255, 196, 64, 245) : IM_COL32(240, 88, 72, 245);
    const char* word = p.sleeping ? "ASLEEP" : "WOUNDED";
    const float mainSize = 23.0f * scale, subSize = 16.0f * scale;
    if (p.state == Rescue::self_asleep || p.state == Rescue::self_wounded || p.state == Rescue::carried) {
        // Asleep, the world stays visible (coop_carry blocks the stock
        // blackout presentation) under DrawSleeperView's dark greyscale.
        const char* head = p.state == Rescue::carried ? "CARRIED" : word;
        const char* tail = p.state == Rescue::carried ? "Your teammate is carrying you" :
            p.sleeping ? "Your teammate can wake you or carry you" : "Crawl to cover. Your teammate can revive you";
        char distance[48]{};
        if (f.tag_fresh && f.tag.dist_m > 0.0f && p.state != Rescue::carried)
            StringCchPrintfA(distance, ARRAYSIZE(distance), "Teammate %.0f m", f.tag.dist_m);
        const ImVec2 hs = font->CalcTextSizeA(mainSize, FLT_MAX, 0.0f, head);
        const ImVec2 ts = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, tail);
        const ImVec2 ds = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, distance);
        // Above the stock sleep/crawl action icons at the lower center.
        const float y = std::floor(display.y * 0.30f);
        const ImU32 headColor = p.state == Rescue::carried ? IM_COL32(235, 235, 235, 245) : status;
        HudText(list, font, mainSize, ImVec2(std::floor((display.x - hs.x) * 0.5f), y), headColor, head);
        HudText(list, font, subSize, ImVec2(std::floor((display.x - ts.x) * 0.5f), y + hs.y + 2.0f * scale), IM_COL32(235, 235, 235, 235), tail);
        if (distance[0])
            HudText(list, font, subSize, ImVec2(std::floor((display.x - ds.x) * 0.5f), y + hs.y + ts.y + 4.0f * scale), IM_COL32(190, 190, 190, 220), distance);
        return;
    }
    // Anchor: just under the name tag's head point, or lower center.
    const bool anchored = f.tag_fresh && f.tag.visible;
    float ax = display.x * 0.5f, ay = display.y * 0.64f;
    if (anchored) {
        nametag::NdcToDisplay(f.tag.rect, f.tag.ndc, display.x, display.y, ax, ay);
        ay += 10.0f * scale;
    }
    const float edge = 24.0f * scale;
    ax = std::fmin(std::fmax(ax, edge + 90.0f * scale), display.x - edge - 90.0f * scale);
    ay = std::fmin(std::fmax(ay, edge), display.y - edge - 60.0f * scale);
    if (p.state == Rescue::away || p.state == Rescue::wait) {
        char text[64]{};
        if (p.state == Rescue::away && f.tag_fresh && f.tag.dist_m > 0.0f)
            StringCchPrintfA(text, ARRAYSIZE(text), anchored ? "%s  %.0f m" : "TEAMMATE %s  %.0f m", word, f.tag.dist_m);
        else StringCchPrintfA(text, ARRAYSIZE(text), anchored ? "%s" : "TEAMMATE %s", word);
        const ImVec2 ts = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, text);
        const float dot = 4.0f * scale, padX = 8.0f * scale, padY = 3.0f * scale;
        const float w = dot * 2.0f + 6.0f * scale + ts.x;
        const float x = std::floor(ax - w * 0.5f), y = std::floor(ay);
        list->AddRectFilled(ImVec2(x - padX, y - padY), ImVec2(x + w + padX, y + ts.y + padY), IM_COL32(10, 12, 14, 150), ts.y);
        list->AddCircleFilled(ImVec2(x + dot, y + ts.y * 0.5f), dot, status, 16);
        HudText(list, font, subSize, ImVec2(x + dot * 2.0f + 6.0f * scale, y), IM_COL32(240, 240, 240, 240), text);
        return;
    }
    // ready / holding / done: [button + ring]  LABEL / HOLD. Within 3 m the
    // teammate lies at the player's feet, where the stock Fulton/Carry icons
    // sit; the button goes beside the player's own character instead.
    const float r = 15.0f * scale;
    const char* label = p.state == Rescue::ready ? (p.sleeping ? "WAKE UP" : "REVIVE") : (p.sleeping ? "WAKING UP" : "REVIVING");
    const char* hint = p.state == Rescue::ready ? "HOLD" : p.state == Rescue::done ? "" : "KEEP HOLDING";
    const ImVec2 ls = font->CalcTextSizeA(mainSize, FLT_MAX, 0.0f, label);
    const ImVec2 hs = font->CalcTextSizeA(subSize, FLT_MAX, 0.0f, hint);
    const float gap = 10.0f * scale, ring = r + 5.0f * scale;
    const float x0 = std::floor(display.x * 0.56f), cy = std::floor(display.y * 0.58f);
    const ImVec2 c(x0 + ring, cy);
    list->AddCircleFilled(c, ring + 1.5f * scale, IM_COL32(10, 12, 14, 140), 40);
    list->AddCircle(c, ring, IM_COL32(255, 255, 255, 70), 40, 3.0f * scale);
    const float progress = p.state == Rescue::done ? 1.0f : p.state == Rescue::holding ? std::fmin(1.0f, std::fmax(0.0f, p.progress)) : 0.0f;
    if (progress > 0.0f) {
        constexpr float kTop = -1.5707963f, kTurn = 6.2831853f;
        list->PathArcTo(c, ring, kTop, kTop + kTurn * progress, 48);
        list->PathStroke(IM_COL32(250, 250, 250, 245), ImDrawFlags_None, 3.5f * scale);
    }
    DrawActionGlyph(list, font, c, r, f.pad_glyphs, 245);
    const float tx = x0 + ring * 2.0f + gap;
    const float blockH = ls.y + (hint[0] ? hs.y : 0.0f);
    const float ty = std::floor(cy - blockH * 0.5f);
    if (hint[0]) HudText(list, font, subSize, ImVec2(tx, ty), IM_COL32(210, 210, 210, 235), hint);
    HudText(list, font, mainSize, ImVec2(tx, ty + (hint[0] ? hs.y : 0.0f)), IM_COL32(255, 255, 255, 250), label);
}
void DrawHudLayer(const HudFrame& f, const ImVec2& display) noexcept {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    if (!list || !(display.x > 0.0f) || !(display.y > 0.0f)) return;
    const float scale = std::fmin(2.0f, std::fmax(0.8f, display.y / 1080.0f));
    ImFont* font = ImGui::GetFont();
    const float size = font->LegacySize * std::fmax(1.0f, std::floor(scale + 0.25f)) * 0.85f;
    const float margin = 14.0f * scale, pad = 5.0f * scale;
    float y = margin;
    const auto right_text = [&](const char* text, ImU32 color, float wrap) noexcept {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, wrap, text);
        const float x = std::floor(display.x - margin - ts.x);
        list->AddRectFilled(ImVec2(x - pad, y - pad * 0.5f), ImVec2(x + ts.x + pad, y + ts.y + pad * 0.5f), IM_COL32(0, 0, 0, 140), 3.0f * scale);
        list->AddText(font, size, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 200), text, nullptr, wrap);
        list->AddText(font, size, ImVec2(x, y), color, text, nullptr, wrap);
        y += ts.y + pad * 2.0f;
    };
    if (f.show_line) {
        right_text(f.line.text, HudColor(f.line.tone, 235), 0.0f);
        if (f.line.detail[0]) right_text(f.line.detail, IM_COL32(200, 200, 200, 220), 0.0f);
    }
    if (f.show_rescue) DrawRescuePrompt(f, list, display, scale);
    const float wrap = std::fmin(display.x * 0.4f, 620.0f * scale);
    for (unsigned i = 0; i < f.toast_count; ++i) right_text(f.toasts[i]->text, HudColor(f.toasts[i]->tone, 245), wrap);
}
#ifdef MGS_DEBUG_OVERLAY_TESTING
// Fixture readback of the backbuffer right after our draw (before Present).
std::atomic<bool> capture_requested{false};
unsigned char* captured = nullptr;
UINT captured_w = 0, captured_h = 0;
void CaptureForTest(IDXGISwapChain* chain) noexcept {
    if (!capture_requested.exchange(false) || !device || !context) return;
    ID3D11Texture2D* back = nullptr; ID3D11Texture2D* staging = nullptr;
    if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) return;
    D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging))) {
        context->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
            std::free(captured);
            captured = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(desc.Width) * desc.Height * 4));
            if (captured) {
                for (UINT y = 0; y < desc.Height; ++y)
                    std::memcpy(captured + static_cast<std::size_t>(y) * desc.Width * 4, static_cast<const unsigned char*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch, desc.Width * 4);
                captured_w = desc.Width; captured_h = desc.Height;
            }
            context->Unmap(staging, 0);
        }
    }
    if (staging) staging->Release();
    back->Release();
}
#endif
void DrawMenu() noexcept {
    debug_menu_ui::Options options{};
    options.worker = worker_scenario::Requested();
    if (!debug_menu_ui::Draw(options)) ChangeOpen(false);
}
void Draw(UINT bb_w, UINT bb_h) noexcept {
    ImGui::SetCurrentContext(gui);
    const bool now_open = open.load(std::memory_order_acquire) && debug_menu::Enabled();
    if (!now_open && open.load(std::memory_order_relaxed)) ChangeOpen(false);
    if (now_open != was_open) {
        if (!now_open) RestoreClip(window.load(std::memory_order_acquire));
        ImGui::GetIO().ClearInputKeys();
        was_open = now_open;
    }
    Message copy[256]{}; unsigned count = 0;
    AcquireSRWLockExclusive(&message_lock);
    count = message_count; std::memcpy(copy, messages, count * sizeof(Message)); message_count = 0;
    ReleaseSRWLockExclusive(&message_lock);
    for (unsigned i = 0; i < count; ++i)
        ImGui_ImplWin32_WndProcHandler(window.load(), copy[i].id, copy[i].wparam, copy[i].lparam);
    ImGuiIO& io = ImGui::GetIO(); io.MouseDrawCursor = now_open;
    nametag::Frame tag_frame{};
    nametag::RenderNote tag_note{};
    const bool tag = NametagFrame(tag_frame, tag_note, bb_w, bb_h);
    HudFrame hud_frame{};
    const bool hud = HudPrepare(hud_frame, now_open);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
    // Closed menu: the display is the backbuffer (the name tag layer only). The
    // DX11 backend maps one ImGui unit to one backbuffer pixel either way, so
    // the tag maps NDC over the backbuffer even under an open menu whose
    // display is the client rect.
    const bool sized = bb_w && bb_h;
    if (!now_open && sized) io.DisplaySize = ImVec2(static_cast<float>(bb_w), static_cast<float>(bb_h));
    ImGui::NewFrame();
    if (hud) DrawSleeperView(hud_frame, sized ? ImVec2(static_cast<float>(bb_w), static_cast<float>(bb_h)) : io.DisplaySize);
    if (tag) DrawNametagLayer(tag_frame, tag_note, sized ? ImVec2(static_cast<float>(bb_w), static_cast<float>(bb_h)) : io.DisplaySize);
    if (hud) DrawHudLayer(hud_frame, sized ? ImVec2(static_cast<float>(bb_w), static_cast<float>(bb_h)) : io.DisplaySize);
    if (!now_open && !tag && !hud) { ImGui::EndFrame(); return; }
    if (now_open) DrawMenu();
    ImGui::Render();
    ExtraState saved(context);
    context->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    saved.Restore(context);
    if (now_open && frames.fetch_add(1, std::memory_order_relaxed) == 0) Log("overlay=drawn; frame=1\r\n");
    if (!now_open && tag && !tag_logged) { tag_logged = true; Log("overlay=nametag_drawn; frame=1\r\n"); }
    if (hud && !hud_logged) { hud_logged = true; Log("overlay=hud_drawn; frame=1\r\n"); }
}
#if MGS_BACKGROUND_WORKER
// Worker builds: the menu is drawn without the Win32 backend, never subclasses
// again and never takes mouse/keyboard; `menu ...` text requests drive it.
LARGE_INTEGER worker_clock{};
std::uint32_t worker_request{};
enum class Deferred : std::uint8_t { none, open, press };
Deferred worker_deferred{};
void WorkerRelease() noexcept {
    if (gui) {
        ImGui::SetCurrentContext(gui);
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(gui);
    }
    gui = nullptr; initialized = false; swapchain = nullptr; worker_clock = {};
    ReleaseSleepFilter(); Release(target); Release(context); Release(device);
}
bool WorkerInitialize(IDXGISwapChain* chain) noexcept {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow || desc.OutputWindow != window.load(std::memory_order_acquire)) return false;
    if (FAILED(chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || FAILED(device->GetDeviceRemovedReason())) {
        WorkerRelease(); return false;
    }
    device->GetImmediateContext(&context);
    if (!context || !CreateTarget(chain)) { WorkerRelease(); return false; }
    gui = ImGui::CreateContext();
    ImGui::SetCurrentContext(gui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    Style(io);
    if (!ImGui_ImplDX11_Init(device, context)) { WorkerRelease(); return false; }
    swapchain = chain; initialized = true;
    Log("overlay=worker_render_ready; api=dx11; input=none\r\n");
    return true;
}
void WorkerDraw(IDXGISwapChain* chain, bool menu, const nametag::Frame* tag_frame, nametag::RenderNote& tag_note, const HudFrame* hud) noexcept {
    ImGui::SetCurrentContext(gui);
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || !desc.BufferDesc.Width || !desc.BufferDesc.Height) {
        if (tag_frame) nametag::NoteRender(tag_note);
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height));
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    const double delta = worker_clock.QuadPart && now.QuadPart > worker_clock.QuadPart && frequency.QuadPart ?
        static_cast<double>(now.QuadPart - worker_clock.QuadPart) / static_cast<double>(frequency.QuadPart) : 1.0 / 60.0;
    io.DeltaTime = delta > 0 && delta < 1.0 ? static_cast<float>(delta) : 1.0f / 60.0f;
    worker_clock = now;
    io.MouseDrawCursor = false;
    ImGui_ImplDX11_NewFrame(); ImGui::NewFrame();
    if (hud) DrawSleeperView(*hud, io.DisplaySize);
    if (tag_frame) DrawNametagLayer(*tag_frame, tag_note, io.DisplaySize);
    if (hud) DrawHudLayer(*hud, io.DisplaySize);
    if (menu) DrawMenu();
    ImGui::Render();
    ExtraState saved(context);
    context->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    saved.Restore(context);
    if (menu && frames.fetch_add(1, std::memory_order_relaxed) == 0) Log("overlay=drawn; frame=1; worker=1\r\n");
    if (!menu && tag_frame && !tag_logged) { tag_logged = true; Log("overlay=nametag_drawn; frame=1; worker=1\r\n"); }
    if (hud && !hud_logged) { hud_logged = true; Log("overlay=hud_drawn; frame=1; worker=1\r\n"); }
}
void WorkerResult(const char* text) noexcept {
    debug_menu::ReportMenuResult(worker_request, text);
    worker_request = 0; worker_deferred = Deferred::none;
}
void ApplyOverlayRequest() noexcept {
    if (worker_request) return;
    std::uint32_t serial = 0;
    char text[debug_menu::kCommandCapacity]{};
    if (!debug_menu::TakeMenuRequest(serial, text)) return;
    worker_request = serial;
    debug_actions::OverlayRequest request{};
    if (!debug_actions::ParseOverlayRequest(text, request)) { WorkerResult("menu_args"); return; }
    char out[160]{};
    debug_actions::Category category{};
    using Op = debug_actions::OverlayOp;
    const bool is_open = open.load(std::memory_order_acquire);
    switch (request.op) {
    case Op::open:
    case Op::toggle:
        if (request.op == Op::toggle && is_open) { ChangeOpen(false); WorkerResult("menu_closed"); return; }
        if (request.arg[0] && debug_actions::ParseCategory(request.arg, category)) debug_menu_ui::SelectCategory(category);
        ChangeOpen(true); worker_deferred = Deferred::open; return;
    case Op::close: ChangeOpen(false); WorkerResult("menu_closed"); return;
    case Op::category:
        debug_actions::ParseCategory(request.arg, category); debug_menu_ui::SelectCategory(category);
        StringCchPrintfA(out, ARRAYSIZE(out), "menu_category=%s", debug_actions::CategoryName(category)); break;
    case Op::search:
        StringCchPrintfA(out, ARRAYSIZE(out), "menu_search;matches=%u", static_cast<unsigned>(debug_menu_ui::SetSearch(request.arg))); break;
    case Op::press:
        if (!debug_menu_ui::BeginPress(request.arg)) { WorkerResult("menu_args"); return; }
        worker_deferred = Deferred::press; return;
    case Op::confirm: debug_menu_ui::Confirm(out, sizeof(out)); break;
    case Op::cancel: debug_menu_ui::Cancel(out, sizeof(out)); break;
    case Op::footer:
        debug_menu_ui::SetFooterExpanded(request.arg[0] == '1');
        StringCchPrintfA(out, ARRAYSIZE(out), "menu_footer=%s", request.arg); break;
    case Op::select:
        if (debug_menu_ui::Select(static_cast<std::uint32_t>(std::strtoul(request.arg, nullptr, 10))))
            StringCchPrintfA(out, ARRAYSIZE(out), "menu_selected=%s", request.arg);
        else StringCchCopyA(out, ARRAYSIZE(out), "not_in_list");
        break;
    default: StringCchCopyA(out, ARRAYSIZE(out), "menu_args"); break;
    }
    WorkerResult(out);
}
void FinishOverlayRequest(bool drawn, bool failed) noexcept {
    if (!worker_request) return;
    char out[160]{};
    if (worker_deferred == Deferred::open) {
        if (failed) WorkerResult("menu_unavailable;reason=init");
        else if (drawn) {
            StringCchPrintfA(out, ARRAYSIZE(out), "menu_open;category=%s;frames=%u",
                debug_actions::CategoryName(debug_menu_ui::CurrentCategory()), frames.load(std::memory_order_relaxed));
            WorkerResult(out);
        }
    } else if (worker_deferred == Deferred::press) {
        if (!drawn) debug_menu_ui::NoteUndrawnFrame();
        if (debug_menu_ui::TakePressResult(out, sizeof(out))) WorkerResult(out);
    }
}
void PublishOverlayStatus() noexcept {
    debug_menu::OverlayStatus status{};
    status.open = open.load(std::memory_order_acquire);
    status.frames = frames.load(std::memory_order_relaxed);
    StringCchCopyA(status.category, ARRAYSIZE(status.category), debug_actions::CategoryName(debug_menu_ui::CurrentCategory()));
    StringCchCopyA(status.search, ARRAYSIZE(status.search), debug_menu_ui::Search());
    debug_menu::SetOverlayStatus(status);
}
// Under render_lock, after the synthetic input pump and before the frame capture,
// so a capture taken in this Present already contains the menu and the name tag.
void WorkerMenu(IDXGISwapChain* chain) noexcept {
    if (gui) ImGui::SetCurrentContext(gui);
    ApplyOverlayRequest();
    bool drawn = false, failed = false;
    const bool menu = open.load(std::memory_order_acquire) && debug_menu::Enabled();
    DXGI_SWAP_CHAIN_DESC desc{};
    const bool sized = SUCCEEDED(chain->GetDesc(&desc));
    nametag::Frame tag_frame{};
    nametag::RenderNote tag_note{};
    const bool tag = NametagFrame(tag_frame, tag_note, sized ? desc.BufferDesc.Width : 0, sized ? desc.BufferDesc.Height : 0);
    HudFrame hud_frame{};
    const bool hud = HudPrepare(hud_frame, menu);
    if (menu || tag || hud) {
        if (initialized && swapchain != chain) WorkerRelease();
        if (initialized || WorkerInitialize(chain)) {
            if (target) { WorkerDraw(chain, menu, tag ? &tag_frame : nullptr, tag_note, hud ? &hud_frame : nullptr); drawn = menu; }
            else if (tag) nametag::NoteRender(tag_note);
        } else {
            if (tag) nametag::NoteRender(tag_note);
            if (menu) { failed = true; ChangeOpen(false); }
        }
    }
    FinishOverlayRequest(drawn, failed);
    PublishOverlayStatus();
}
#endif
HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain, UINT interval, UINT flags) noexcept {
    presents.fetch_add(1, std::memory_order_relaxed);
    const DWORD incoming_error = GetLastError();
    if (worker_scenario::Requested() && (flags & DXGI_PRESENT_TEST) == 0 && !resizing.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&render_lock);
        if (!window.load() && worker_scenario::ThreadAdmitted()) {
            DXGI_SWAP_CHAIN_DESC desc{}; DWORD pid = 0; wchar_t desktop[256]{}; DWORD needed = 0;
            const auto run = worker_scenario::GetContext();
            const DWORD thread = SUCCEEDED(chain->GetDesc(&desc)) && desc.OutputWindow ? GetWindowThreadProcessId(desc.OutputWindow, &pid) : 0;
            if (thread && pid == GetCurrentProcessId() && GetUserObjectInformationW(GetThreadDesktop(thread), UOI_NAME, desktop, sizeof(desktop), &needed) &&
                needed <= sizeof(desktop) && !std::wcscmp(desktop, run.desktop)) {
                SetLastError(ERROR_SUCCESS);
                const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
                if (previous || GetLastError() == ERROR_SUCCESS) {original_wndproc.store(previous);window.store(desc.OutputWindow);}
                else worker_scenario::Refuse("worker_wndproc");
            }
        }
#if MGS_BACKGROUND_WORKER
        if (window.load() && worker_scenario::ThreadAdmitted()) { PumpSyntheticInput(window.load()); WorkerMenu(chain); }
#endif
        worker_frames::Capture(chain, worker_scenario::TakeCapture(GetTickCount64()));
        ReleaseSRWLockExclusive(&render_lock);
    }
    if (!worker_scenario::Requested() && (flags & DXGI_PRESENT_TEST) == 0 && (debug_menu::Enabled() || nametag::Enabled() || coop_hud::Enabled() || coop_hud::RescueEnabled()) &&
        !resizing.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&render_lock);
        if (initialized && swapchain != chain) Cleanup();
        if (initialized || Initialize(chain)) {
            DXGI_SWAP_CHAIN_DESC desc{};
            const bool sized = SUCCEEDED(chain->GetDesc(&desc));
            if (target && !resizing.load(std::memory_order_relaxed)) Draw(sized ? desc.BufferDesc.Width : 0, sized ? desc.BufferDesc.Height : 0);
#ifdef MGS_DEBUG_OVERLAY_TESTING
            CaptureForTest(chain);
#endif
        }
        ReleaseSRWLockExclusive(&render_lock);
    }
    // DXGI may wait on the window's message thread. Never hold our lock here.
    SetLastError(incoming_error);
    const HRESULT result = original_present(chain, interval, flags);
    const DWORD original_error = GetLastError();
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        AcquireSRWLockExclusive(&render_lock);
        if (swapchain == chain) {
#if MGS_BACKGROUND_WORKER
            if (worker_scenario::Requested()) WorkerRelease(); else
#endif
            Cleanup();
        }
        ReleaseSRWLockExclusive(&render_lock);
        Log("overlay=device_lost; resources=released\r\n");
    }
    SetLastError(original_error); return result;
}
HRESULT STDMETHODCALLTYPE Resize(IDXGISwapChain* chain, UINT buffers, UINT width, UINT height, DXGI_FORMAT format, UINT flags) noexcept {
    const DWORD incoming_error = GetLastError();
    resizing.store(true, std::memory_order_release);
    AcquireSRWLockExclusive(&render_lock);
    if (worker_scenario::Requested()) worker_frames::Reset();
    const bool ours = initialized && swapchain == chain;
    if (ours) { Release(target); ImGui::SetCurrentContext(gui); ImGui_ImplDX11_InvalidateDeviceObjects(); }
    ReleaseSRWLockExclusive(&render_lock);
    SetLastError(incoming_error);
    const HRESULT result = original_resize(chain, buffers, width, height, format, flags);
    const DWORD original_error = GetLastError();
    AcquireSRWLockExclusive(&render_lock);
    if (ours && initialized && swapchain == chain) CreateTarget(chain);
    ReleaseSRWLockExclusive(&render_lock);
    resizing.store(false, std::memory_order_release);
    resizes.fetch_add(1, std::memory_order_relaxed);
    Log(SUCCEEDED(result) ? "overlay=resize; result=ok\r\n" : "overlay=resize; result=failed\r\n");
    SetLastError(original_error); return result;
}
BOOL CALLBACK InstallOnce(PINIT_ONCE, PVOID, PVOID*) noexcept {
    if (!debug_menu::Enabled() && !nametag::Enabled() && !coop_hud::Enabled() && !coop_hud::RescueEnabled() && !worker_scenario::Requested()) return TRUE;
    if (worker_scenario::Requested() && !worker_scenario::GetContext().run_id) return TRUE;
    InitializeLog();
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        StringCchPrintfA(install_line, ARRAYSIZE(install_line), "overlay=refused; reason=minhook; mh=%s(%d)\r\n", MH_StatusToString(status), static_cast<int>(status));
        Log(install_line); return TRUE;
    }
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"MGSCoopDX11Probe";
    const ATOM atom = RegisterClassExW(&cls);
    if (!atom) {
        StringCchPrintfA(install_line, ARRAYSIZE(install_line), "overlay=refused; reason=probe_class; error=%lu\r\n", GetLastError());
        Log(install_line); return TRUE;
    }
    const HWND hidden = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, cls.hInstance, nullptr);
    if (!hidden) Fail("window", "CreateWindowExW", FailKind::win32, static_cast<long>(GetLastError()));
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = hidden;
    desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* dummy = nullptr; ID3D11Device* dummy_device = nullptr; ID3D11DeviceContext* dummy_context = nullptr;
    const HRESULT created = hidden ? D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &desc, &dummy, &dummy_device, nullptr, &dummy_context) : E_FAIL;
    bool ok = SUCCEEDED(created) && dummy;
    if (hidden && !ok) Fail("device", "D3D11CreateDeviceAndSwapChain", FailKind::hr, created);
    if (ok) {
        void** methods = *reinterpret_cast<void***>(dummy);
        ok = StepHook("present_hook", "IDXGISwapChain_Present", present_hook, methods[8], reinterpret_cast<void*>(&Present)) &&
             StepHook("resize_hook", "IDXGISwapChain_ResizeBuffers", resize_hook, methods[13], reinterpret_cast<void*>(&Resize));
        original_present = reinterpret_cast<PresentFn>(present_hook.original);
        original_resize = reinterpret_cast<ResizeFn>(resize_hook.original);
        if (ok) ok = InputHooks() && EnableHooks();
    }
    if (!ok) RemoveHooks();
    if (worker_scenario::Requested()) worker_scenario::SetHooksReady(ok);
    Release(dummy_context); Release(dummy_device); Release(dummy);
    if (hidden) DestroyWindow(hidden);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    if (ok) StringCchCopyA(install_line, ARRAYSIZE(install_line), "overlay=installed; api=dx11; toggle=F9; graphics=Present_ResizeBuffers\r\n");
    else if (!RefusalLine(install_failure, install_line, ARRAYSIZE(install_line)))
        StringCchCopyA(install_line, ARRAYSIZE(install_line), "overlay=refused; reason=graphics_or_input_hook\r\n");
    Log(install_line);
    return TRUE;
}
} // namespace

static_assert(sizeof(XINPUT_STATE)==sizeof(worker_scenario::VirtualPadState) &&
    offsetof(XINPUT_STATE,Gamepad)==4 && offsetof(XINPUT_GAMEPAD,sThumbRX)==8,"virtual pad Windows ABI");
bool WorkerVirtualPadState(unsigned index,void* output,std::size_t bytes) noexcept {
    if(!output || bytes!=sizeof(XINPUT_STATE))return false;
    const DWORD error=GetLastError();std::memset(output,0,bytes);
    bool supplied=false;
    if(worker_scenario::Requested()) {
        // Hidden menu input follows a separate path from CapturingInput().
        // Disarm, so closing F9 cannot revive an earlier analog hold.
        if(open.load(std::memory_order_acquire)) worker_scenario::ClearVirtualPad();
        else supplied=worker_scenario::ReadVirtualPad(index,output,bytes);
    }
    SetLastError(error);return supplied;
}
void SetShutdownHandler(ShutdownFn fn) noexcept { shutdown_handler.store(fn, std::memory_order_release); }

bool CapturingInput() noexcept {
    if (worker_scenario::Requested()) return false;
    if (!debug_menu::Enabled() || !Foreground()) return false;
    if (open.load(std::memory_order_acquire)) return true;
    if (!releasing.load(std::memory_order_acquire)) return false;
#ifdef MGS_DEBUG_OVERLAY_TESTING
    if (test_mouse_held.load(std::memory_order_acquire)) return true;
#else
    const int held[] = {VK_F9, VK_ESCAPE, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (const int key : held) if (GetAsyncKeyState(key) & 0x8000) return true;
#endif
    if (!PadsReleased()) return true;
    if (GetTickCount64() < release_after.load(std::memory_order_acquire)) return true;
    releasing.store(false, std::memory_order_release);
    return false;
}
WorkerInputStats GetWorkerInputStats() noexcept {
    WorkerInputStats out{};
#if MGS_BACKGROUND_WORKER
    out.raw_keyboard_posted = worker_counters[static_cast<unsigned>(WorkerCounter::raw_keyboard_posted)].load(std::memory_order_relaxed);
    out.raw_mouse_posted = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_posted)].load(std::memory_order_relaxed);
    out.raw_post_failed = worker_counters[static_cast<unsigned>(WorkerCounter::raw_post_failed)].load(std::memory_order_relaxed);
    out.raw_mouse_axes_posted = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_axes_posted)].load(std::memory_order_relaxed);
    out.raw_mouse_buttons_posted = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_buttons_posted)].load(std::memory_order_relaxed);
    out.raw_keyboard_reads = worker_counters[static_cast<unsigned>(WorkerCounter::raw_keyboard_reads)].load(std::memory_order_relaxed);
    out.raw_mouse_reads = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_reads)].load(std::memory_order_relaxed);
    out.raw_mouse_axes_reads = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_axes_reads)].load(std::memory_order_relaxed);
    out.raw_mouse_buttons_reads = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_buttons_reads)].load(std::memory_order_relaxed);
    out.raw_keyboard_packets_read = worker_counters[static_cast<unsigned>(WorkerCounter::raw_keyboard_packets_read)].load(std::memory_order_relaxed);
    out.raw_mouse_packets_read = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_packets_read)].load(std::memory_order_relaxed);
    out.raw_mouse_axis_packets_read = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_axis_packets_read)].load(std::memory_order_relaxed);
    out.raw_mouse_button_packets_read = worker_counters[static_cast<unsigned>(WorkerCounter::raw_mouse_button_packets_read)].load(std::memory_order_relaxed);
    out.raw_read_refused = worker_counters[static_cast<unsigned>(WorkerCounter::raw_read_refused)].load(std::memory_order_relaxed);
    out.cursor_success = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_success)].load(std::memory_order_relaxed);
    out.cursor_null = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_null)].load(std::memory_order_relaxed);
    out.cursor_thread_refused = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_thread_refused)].load(std::memory_order_relaxed);
    out.cursor_window_refused = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_window_refused)].load(std::memory_order_relaxed);
    out.cursor_geometry_refused = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_geometry_refused)].load(std::memory_order_relaxed);
    out.cursor_map_refused = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_map_refused)].load(std::memory_order_relaxed);
    out.cursor_window_match = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_window_match)].load(std::memory_order_relaxed);
    out.cursor_window_mismatch = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_window_mismatch)].load(std::memory_order_relaxed);
    out.cursor_present_missing = worker_counters[static_cast<unsigned>(WorkerCounter::cursor_present_missing)].load(std::memory_order_relaxed);
    out.di_mouse_state_calls = worker_counters[static_cast<unsigned>(WorkerCounter::di_mouse_state_calls)].load(std::memory_order_relaxed);
    out.di_mouse_state_supplied = worker_counters[static_cast<unsigned>(WorkerCounter::di_mouse_state_supplied)].load(std::memory_order_relaxed);
    out.di_mouse_state_axes = worker_counters[static_cast<unsigned>(WorkerCounter::di_mouse_state_axes)].load(std::memory_order_relaxed);
    out.di_mouse_state_buttons = worker_counters[static_cast<unsigned>(WorkerCounter::di_mouse_state_buttons)].load(std::memory_order_relaxed);
    out.di_mouse_buffer_calls = worker_counters[static_cast<unsigned>(WorkerCounter::di_mouse_buffer_calls)].load(std::memory_order_relaxed);
    out.di_mouse_buffer_events = worker_counters[static_cast<unsigned>(WorkerCounter::di_mouse_buffer_events)].load(std::memory_order_relaxed);
    out.di_mouse_buffer_peek_events = worker_counters[static_cast<unsigned>(WorkerCounter::di_mouse_buffer_peek_events)].load(std::memory_order_relaxed);
#endif
    return out;
}
void Install() noexcept {
    const DWORD error = GetLastError();
    if (debug_menu::Enabled() || nametag::Enabled() || coop_hud::Enabled() || coop_hud::RescueEnabled() || worker_scenario::Requested())
        InitOnceExecuteOnce(&install_once, &InstallOnce, nullptr, nullptr);
    SetLastError(error);
}
#ifdef MGS_DEBUG_OVERLAY_TESTING
bool RefusalLineForTest(const char* step, const char* detail, unsigned kind, long code, char* out, std::size_t capacity) noexcept {
    return RefusalLine(InstallFailure{step, detail, static_cast<FailKind>(kind), code}, out, capacity);
}
bool InstallLineForTest(char* out, std::size_t capacity) noexcept {
    return install_line[0] && out && SUCCEEDED(StringCchCopyA(out, capacity, install_line));
}
void SetMouseHeldForTest(bool value) noexcept { test_mouse_held.store(value, std::memory_order_release); }
void SetOpenForTest(bool value) noexcept { ChangeOpen(value); }
void SetForegroundForTest(bool value) noexcept { test_foreground.store(value); }
bool HookPadFixtureForTest(void* function) noexcept {
    return AddHook(fixture_pad, function, reinterpret_cast<void*>(&Pad)) && MH_EnableHook(fixture_pad.target) == MH_OK;
}
bool HookInputFixtureForTest(void* state, void* data) noexcept {
    return AddHook(fixture_state, state, reinterpret_cast<void*>(&DeviceState)) &&
        AddHook(fixture_data, data, reinterpret_cast<void*>(&DeviceData)) &&
        MH_EnableHook(fixture_state.target) == MH_OK && MH_EnableHook(fixture_data.target) == MH_OK;
}
void RawKeyForTest(unsigned key) noexcept { RAWKEYBOARD raw{}; raw.VKey = static_cast<USHORT>(key); raw.MakeCode = static_cast<USHORT>(MapVirtualKeyW(key, MAPVK_VK_TO_VSC)); RawText(raw); }
TestStats StatsForTest() noexcept {
    AcquireSRWLockShared(&render_lock);
    const TestStats result{presents.load(), frames.load(), resizes.load(), input_messages.load(), characters.load(), initialized, open.load()};
    ReleaseSRWLockShared(&render_lock); return result;
}
void CaptureNextFrameForTest() noexcept { capture_requested.store(true); }
bool CapturedFrameForTest(const unsigned char*& rgba, unsigned& width, unsigned& height) noexcept {
    rgba = captured; width = captured_w; height = captured_h;
    return captured != nullptr;
}
void ShutdownForTest() noexcept {
    AcquireSRWLockExclusive(&render_lock); Cleanup(); ReleaseSRWLockExclusive(&render_lock);
    RemoveHooks();
    RemoveHook(fixture_state); RemoveHook(fixture_data); RemoveHook(fixture_pad);
#if MGS_BACKGROUND_WORKER && defined(MGS_WORKER_TESTING)
    RemoveHook(fixture_format); RemoveHook(fixture_coop); RemoveHook(fixture_raw_buffer); RemoveHook(fixture_raw_input);
    fixture_cursor = {};
    for (auto& counter : worker_counters) counter.store(0, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&synthetic_lock);
    for (auto& slot : synthetic_ring) slot = {};
    synthetic_next = 0;
    ReleaseSRWLockExclusive(&synthetic_lock);
#endif
    if (log_file != INVALID_HANDLE_VALUE) CloseHandle(log_file);
    log_file = INVALID_HANDLE_VALUE;
}
#if MGS_BACKGROUND_WORKER && defined(MGS_WORKER_TESTING)
bool HookWorkerDeviceFixtureForTest(void* state, void* data, void* format, void* coop) noexcept {
    return HookInputFixtureForTest(state, data) &&
        AddHook(fixture_format, format, reinterpret_cast<void*>(&WorkerFormat)) &&
        AddHook(fixture_coop, coop, reinterpret_cast<void*>(&WorkerCoop)) &&
        MH_EnableHook(fixture_format.target) == MH_OK && MH_EnableHook(fixture_coop.target) == MH_OK;
}
bool HookWorkerRawBufferFixtureForTest(void* original) noexcept {
    return AddHook(fixture_raw_buffer, original, reinterpret_cast<void*>(&WorkerRawBuffer)) &&
        MH_EnableHook(fixture_raw_buffer.target) == MH_OK;
}
bool HookWorkerRawInputFixtureForTest(void* original) noexcept {
    return AddHook(fixture_raw_input, original, reinterpret_cast<void*>(&WorkerRawInput)) &&
        MH_EnableHook(fixture_raw_input.target) == MH_OK;
}
HRESULT WorkerStateForTest(IDirectInputDevice8W* object, DWORD bytes, void* state) noexcept { return DeviceState(object, bytes, state); }
HRESULT WorkerDataForTest(IDirectInputDevice8W* object, DWORD bytes, DIDEVICEOBJECTDATA* data, DWORD* count, DWORD flags) noexcept { return DeviceData(object, bytes, data, count, flags); }
HRESULT WorkerFormatForTest(IDirectInputDevice8W* object, const DIDATAFORMAT* format) noexcept { return WorkerFormat(object, format); }
HRESULT WorkerCoopForTest(IDirectInputDevice8W* object, HWND hwnd, DWORD flags) noexcept { return WorkerCoop(object, hwnd, flags); }
BOOL WorkerCursorForTest(int x, int y) noexcept { return CursorPos(x, y); }
void ConfigureWorkerCursorFixtureForTest(HWND own, ClientRectFn rect, ClientToScreenFn to_screen, GetCursorPosFn original) noexcept {
    fixture_cursor = {true, own, rect, to_screen, original};
}
BOOL WorkerGetCursorForTest(LPPOINT point) noexcept { return WorkerGetCursorPos(point); }
void SetWorkerPresentWindowForTest(HWND own) noexcept { window.store(own); }
BOOL WorkerClipForTest(const RECT* rect) noexcept { return Clip(rect); }
SHORT WorkerAsyncForTest(int key) noexcept { return WorkerAsync(key); }
SHORT WorkerKeyStateForTest(int key) noexcept { return WorkerKeyState(key); }
UINT WorkerRawBufferForTest(RAWINPUT* output, UINT* size, UINT header) noexcept { return WorkerRawBuffer(output,size,header); }
UINT WorkerRawInputForTest(HRAWINPUT input,UINT command,void* output,UINT* size,UINT header) noexcept { return WorkerRawInput(input,command,output,size,header); }
HRAWINPUT WorkerSyntheticLoadingForTest(std::uint32_t mission,std::uint32_t generation,std::uint64_t expires) noexcept {
    worker_scenario::Event event{};event.offset=0x39;event.value=0x80;
    event.loading_mission=mission;event.loading_generation=generation;event.expires=expires;
    return reinterpret_cast<HRAWINPUT>(SyntheticStore(SyntheticKeyboard(event),&event));
}
HRAWINPUT WorkerSyntheticKeyboardForTest(unsigned scan,bool down) noexcept {
    worker_scenario::Event event{};event.offset=scan;event.value=down?0x80u:0u;
    return reinterpret_cast<HRAWINPUT>(SyntheticStore(SyntheticKeyboard(event)));
}
DWORD WorkerPadForTest(DWORD index, XINPUT_STATE* state) noexcept { return Pad(index, state); }
#endif
#endif
} // namespace mgscoop::debug_overlay
