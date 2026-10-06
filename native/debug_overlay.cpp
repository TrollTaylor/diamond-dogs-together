// Original DX11 debug overlay. No game addresses, native world calls or Lua.
// Dear ImGui core/backends: MIT, pinned in third_party/imgui/UPSTREAM.txt.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dinput.h>
#include <xinput.h>
#include <strsafe.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <cmath>
#include <cwchar>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "mgscoop/debug_overlay.hpp"
#include "mgscoop/debug_menu.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace mgscoop::debug_overlay {
namespace {
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using InputCreateFn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using InputStateFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);
using InputDataFn = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using CursorPosFn = BOOL (WINAPI*)(int, int);
using ClipFn = BOOL (WINAPI*)(const RECT*);
using KeyboardFn = BOOL (WINAPI*)(PBYTE);
using PadFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
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
#ifdef MGS_DEBUG_OVERLAY_TESTING
Hook fixture_state{}, fixture_data{}, fixture_pad{};
std::atomic<bool> test_foreground{true};
#endif
PresentFn original_present = nullptr;
ResizeFn original_resize = nullptr;
std::atomic<WNDPROC> original_wndproc{nullptr};
IDXGISwapChain* swapchain = nullptr; // borrowed only during its callbacks
ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
ID3D11RenderTargetView* target = nullptr;
ImGuiContext* gui = nullptr;
bool initialized = false, clip_saved = false, restore_unclipped = false, was_open = false;
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
#ifdef MGS_DEBUG_OVERLAY_TESTING
    (void)rect; // hidden tests never change the user's global cursor clip
#else
    if (clip_hook.original) reinterpret_cast<ClipFn>(clip_hook.original)(rect);
#endif
}
void SaveClip() noexcept {
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
    const bool prior = f9_down.exchange(down, std::memory_order_acq_rel);
    if (down && !prior && debug_menu::Enabled() && Foreground()) ChangeOpen(!open.load(std::memory_order_acquire));
}
bool InputMessage(UINT id) noexcept {
    return (id >= WM_KEYFIRST && id <= WM_KEYLAST) ||
           (id >= WM_MOUSEFIRST && id <= WM_MOUSELAST) || id == WM_INPUT;
}
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
LRESULT CALLBACK WindowProc(HWND hwnd, UINT id, WPARAM wp, LPARAM lp) noexcept {
    const WNDPROC previous = original_wndproc.load(std::memory_order_acquire);
    bool toggle_event = false;
    if ((id == WM_KEYDOWN || id == WM_SYSKEYDOWN || id == WM_KEYUP || id == WM_SYSKEYUP) && wp == VK_F9) {
        F9(id == WM_KEYDOWN || id == WM_SYSKEYDOWN);
        toggle_event = debug_menu::Enabled() && Foreground();
    }
    if (id == WM_INPUT) {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1)) {
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
        ChangeOpen(false);
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
void RemoveHook(Hook& hook) noexcept {
    if (hook.target) { MH_DisableHook(hook.target); MH_RemoveHook(hook.target); }
    hook = {};
}
void RemoveHooks() noexcept {
    RemoveHook(present_hook); RemoveHook(resize_hook);
    for (auto& hook : state_hooks) RemoveHook(hook);
    for (auto& hook : data_hooks) RemoveHook(hook);
    RemoveHook(cursor_hook); RemoveHook(clip_hook); RemoveHook(keyboard_hook); RemoveHook(pad_hook);
}
bool EnableHooks() noexcept {
    Hook* hooks[] = {&present_hook, &resize_hook, &state_hooks[0], &state_hooks[1],
                    &data_hooks[0], &data_hooks[1], &cursor_hook, &clip_hook, &keyboard_hook, &pad_hook};
    for (const auto* hook : hooks)
        if (hook->target && MH_QueueEnableHook(hook->target) != MH_OK) return false;
    return MH_ApplyQueued() == MH_OK;
}
template<class Function>
Function DeviceOriginal(IDirectInputDevice8W* self, unsigned index, const Hook (&hooks)[2]) noexcept {
    void* address = (*reinterpret_cast<void***>(self))[index];
    for (const auto& hook : hooks)
        if (hook.target == address) return reinterpret_cast<Function>(hook.original);
#ifdef MGS_DEBUG_OVERLAY_TESTING
    const auto& fixture = index == 9 ? fixture_state : fixture_data;
    if (fixture.target == address) return reinterpret_cast<Function>(fixture.original);
#endif
    return nullptr;
}
bool KeyboardOrMouse(IDirectInputDevice8W* self) noexcept {
    DIDEVICEINSTANCEW info{}; info.dwSize = sizeof(info);
    if (FAILED(self->GetDeviceInfo(&info))) return false;
    const DWORD type = GET_DIDEVICE_TYPE(info.dwDevType);
    return type == DI8DEVTYPE_KEYBOARD || type == DI8DEVTYPE_MOUSE;
}
HRESULT STDMETHODCALLTYPE DeviceState(IDirectInputDevice8W* self, DWORD bytes, LPVOID output) noexcept {
    const auto original = DeviceOriginal<InputStateFn>(self, 9, state_hooks);
    if (!original) return DIERR_GENERIC;
    const HRESULT result = original(self, bytes, output);
    const DWORD error = GetLastError();
    if (SUCCEEDED(result) && output && CapturingInput() && KeyboardOrMouse(self) &&
        (bytes == 256 || bytes == sizeof(DIMOUSESTATE) || bytes == sizeof(DIMOUSESTATE2)))
        std::memset(output, 0, bytes);
    SetLastError(error);
    return result;
}
HRESULT STDMETHODCALLTYPE DeviceData(IDirectInputDevice8W* self, DWORD bytes, LPDIDEVICEOBJECTDATA output, LPDWORD count, DWORD flags) noexcept {
    const auto original = DeviceOriginal<InputDataFn>(self, 10, data_hooks);
    if (!original) return DIERR_GENERIC;
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
    if (CapturingInput()) { SetLastError(error); return TRUE; }
    SetLastError(error);
    return reinterpret_cast<CursorPosFn>(cursor_hook.original)(x, y);
}
BOOL WINAPI Clip(const RECT* rect) noexcept {
    const DWORD error = GetLastError();
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
        if (g.wButtons || g.bLeftTrigger || g.bRightTrigger ||
            g.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || g.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
            g.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || g.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE ||
            g.sThumbRX > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || g.sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE ||
            g.sThumbRY > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || g.sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
            released = false;
    }
    SetLastError(error);
    return released;
}
DWORD WINAPI Pad(DWORD index, XINPUT_STATE* state) noexcept {
    const DWORD result = PadOriginal()(index, state);
    const DWORD error = GetLastError();
    if (result == ERROR_SUCCESS && state && CapturingInput()) std::memset(&state->Gamepad, 0, sizeof(state->Gamepad));
    SetLastError(error);
    return result;
}
bool InputHooks() noexcept {
    wchar_t input_path[MAX_PATH]{};
    if (!GetSystemDirectoryW(input_path, ARRAYSIZE(input_path)) ||
        FAILED(StringCchCatW(input_path, ARRAYSIZE(input_path), L"\\dinput8.dll"))) return false;
    const HMODULE input_module = LoadLibraryExW(input_path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto create = input_module ? reinterpret_cast<InputCreateFn>(GetProcAddress(input_module, "DirectInput8Create")) : nullptr;
    IDirectInput8W* input = nullptr;
    if (!create || FAILED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&input), nullptr))) return false;
    const GUID* identifiers[] = {&GUID_SysKeyboard, &GUID_SysMouse};
    bool ok = true;
    for (unsigned i = 0; i < ARRAYSIZE(identifiers) && ok; ++i) {
        IDirectInputDevice8W* object = nullptr;
        if (FAILED(input->CreateDevice(*identifiers[i], &object, nullptr))) { ok = false; break; }
        void** methods = *reinterpret_cast<void***>(object);
        if (i == 0 || methods[9] != state_hooks[0].target)
            ok = AddHook(state_hooks[i], methods[9], reinterpret_cast<void*>(&DeviceState));
        if (ok && (i == 0 || methods[10] != data_hooks[0].target))
            ok = AddHook(data_hooks[i], methods[10], reinterpret_cast<void*>(&DeviceData));
        object->Release();
    }
    input->Release();
    const HMODULE user = GetModuleHandleW(L"user32.dll");
    if (ok) ok = AddHook(cursor_hook, reinterpret_cast<void*>(GetProcAddress(user, "SetCursorPos")), reinterpret_cast<void*>(&CursorPos));
    if (ok) ok = AddHook(clip_hook, reinterpret_cast<void*>(GetProcAddress(user, "ClipCursor")), reinterpret_cast<void*>(&Clip));
    if (ok) ok = AddHook(keyboard_hook, reinterpret_cast<void*>(GetProcAddress(user, "GetKeyboardState")), reinterpret_cast<void*>(&Keyboard));
    const HMODULE xinput = GetModuleHandleW(L"XINPUT1_3.dll");
    if (ok && xinput) ok = AddHook(pad_hook, reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetState")), reinterpret_cast<void*>(&Pad));
    return ok;
}

template<class T> void Release(T*& value) noexcept { if (value) value->Release(); value = nullptr; }
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
    Release(target); Release(context); Release(device);
    AcquireSRWLockExclusive(&message_lock); message_count = 0; ReleaseSRWLockExclusive(&message_lock);
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
    ImFontConfig font{}; font.SizePixels = 17.0f; io.Fonts->AddFontDefault(&font);
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f; style.FrameRounding = 4.0f;
    style.WindowPadding = ImVec2(18, 16); style.ItemSpacing = ImVec2(10, 9);
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
const char* footer_status = "Choose an action; its game-thread result appears below.";
void Send(const char* command) noexcept {
    debug_menu::QueueCommand(command);
}
bool Action(const char* label, const char* command) noexcept {
    if (!ImGui::Button(label)) return false;
    Send(command); return true;
}
void Pose(const char* label, const float (&pos)[3]) noexcept {
    ImGui::Text("%s  X %.2f   Y %.2f   Z %.2f m", label, pos[0], pos[1], pos[2]);
}
void Overview(const debug_menu::Snapshot& s) noexcept {
    ImGui::Text("Link: %s   RTT %u ms   jitter %u ms", s.link_state[0] ? s.link_state : "unavailable", s.rtt_ms, s.jitter_ms);
    ImGui::Text("Loss %.1f%%   interpolation %u ms   pose age %u ms", s.loss_permille / 10.0f, s.interp_ms, s.remote_age_ms);
    ImGui::Separator();
    ImGui::Text("Partner: %s   appearance: %s   range: %s", s.realized ? "visible" : "hidden", s.parts_loaded ? "loaded" : "pending", s.range_hidden ? "too far" : "near");
    ImGui::Text("Stage %u   source %s   movement %s   motion %s", s.stage, s.source, s.apply, s.dm);
    ImGui::Text("Animation %s   look %s   input %s", s.anim, s.look, s.input_armed ? "armed" : "off");
    if (s.pose_valid) { Pose("You", s.player_pos); Pose("Remote", s.remote_pos); }
    if (s.body_valid) {
        Pose("Partner body", s.body_pos);
        ImGui::Text("Body / remote error %.3f m   heading error %.1f degrees", s.position_error_m, s.heading_error_deg);
        ImGui::Text("Remote yaw %.1f   body yaw %.1f degrees", s.remote_yaw, s.body_yaw);
    }
    if (s.refusal[0]) ImGui::TextWrapped("Readiness: %s", s.refusal);
    ImGui::Spacing();
    Action("Read player position", "position");
    ImGui::TextWrapped("F9 closes this menu. Gameplay input is captured while it is open.");
}
void Movement(const debug_menu::Snapshot& s) noexcept {
    ImGui::TextUnformatted("Local partner controls");
    ImGui::Text("Current: stage %u / %s / %s / animation %s", s.stage, s.apply, s.dm, s.anim);
    if (s.body_valid) ImGui::Text("Body yaw %.1f   remote yaw %.1f   heading error %.1f degrees", s.body_yaw, s.remote_yaw, s.heading_error_deg);
    Action("Hide partner", "partner stage=0"); ImGui::SameLine();
    Action("Show partner", "partner stage=3 source=net look=net");
    Action("Default network movement", "partner stage=3 source=net apply=xform anim=off input=off dm=map look=net");
    Action("Warp movement", "partner stage=3 source=net apply=warp anim=off input=off dm=off look=net");
    ImGui::Separator();
    static int movement = 0, animation = 0, motion = 0;
    const char* movement_names[] = {"Transform", "Warp", "Hybrid", "Input"};
    const char* movement_values[] = {"xform", "warp", "hybrid", "input"};
    const char* animation_names[] = {"Off", "Network", "Mirror local"};
    const char* animation_values[] = {"off", "net", "mirror"};
    const char* motion_names[] = {"Automatic map", "Off", "Manual"};
    const char* motion_values[] = {"map", "off", "on"};
    ImGui::SetNextItemWidth(210); ImGui::Combo("Movement", &movement, movement_names, ARRAYSIZE(movement_names));
    ImGui::SetNextItemWidth(210); ImGui::Combo("Animation", &animation, animation_names, ARRAYSIZE(animation_names));
    ImGui::SetNextItemWidth(210); ImGui::Combo("Direct motion", &motion, motion_names, ARRAYSIZE(motion_names));
    if (ImGui::Button("Apply selected modes")) {
        char command[180]{};
        StringCchPrintfA(command, ARRAYSIZE(command), "partner stage=3 source=net apply=%s input=%s anim=%s aset=qs dm=%s look=net", movement_values[movement], movement == 3 ? "net" : "off", animation_values[animation], motion_values[motion]);
        Send(command);
    }
    static int clip = 0;
    ImGui::SetNextItemWidth(210); ImGui::SliderInt("Manual animation clip", &clip, 0, 12);
    if (ImGui::Button("Play clip")) {
        char command[64]{}; StringCchPrintfA(command, ARRAYSIZE(command), "partner dm=on dmplay=%d", clip); Send(command);
    }
    ImGui::SameLine(); Action("Stop clip", "partner dmstop=1");
}
void Player(const debug_menu::Snapshot& s) noexcept {
    ImGui::TextUnformatted("Local player / copied save session");
    Action("Read equipped inventory", "inventory");
    static float destination[3] = {}, yaw = 0, step[3] = {0, 0, 2};
    if (ImGui::Button("Use current position") && s.pose_valid) { std::memcpy(destination, s.player_pos, sizeof(destination)); yaw = s.player_yaw; }
    ImGui::SetNextItemWidth(330); ImGui::InputFloat3("Teleport XYZ (m)", destination, "%.3f");
    ImGui::SetNextItemWidth(150); ImGui::InputFloat("Yaw (degrees)", &yaw, 0, 0, "%.2f");
    if (ImGui::Button("Teleport")) {
        char command[160]{}; StringCchPrintfA(command, ARRAYSIZE(command), "teleport %.3f %.3f %.3f %.3f", destination[0], destination[1], destination[2], yaw); Send(command);
    }
    ImGui::SetNextItemWidth(330); ImGui::InputFloat3("Offset XYZ (m)", step, "%.2f");
    if (ImGui::Button("Move by offset")) {
        char command[128]{}; StringCchPrintfA(command, ARRAYSIZE(command), "step %.3f %.3f %.3f", step[0], step[1], step[2]); Send(command);
    }
    static int bookmark = 0;
    ImGui::SetNextItemWidth(150); ImGui::SliderInt("Bookmark", &bookmark, 0, 7);
    if (ImGui::Button("Save position")) {
        char command[32]{}; StringCchPrintfA(command, ARRAYSIZE(command), "bookmark %d", bookmark); Send(command);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled((s.bookmark_mask & (1u << bookmark)) == 0);
    if (ImGui::Button("Return")) {
        char command[32]{}; StringCchPrintfA(command, ARRAYSIZE(command), "return %d", bookmark); Send(command);
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    bool ammo = s.infinite_ammo;
    if (ImGui::Checkbox("Infinite ammunition", &ammo)) Send(ammo ? "infinite_ammo 1" : "infinite_ammo 0");
    Action("Refill current hip", "weapon hip current"); ImGui::SameLine();
    Action("Refill current back", "weapon back current");
    Action("Refill current secondary", "weapon secondary current");
    ImGui::TextWrapped("Refill uses current loaded equipment. Select the gun with normal game controls. A different unloaded preset is refused.");
    static int weapon = 0, slot = 0;
    const char* weapon_names[] = {"Secondary 10101", "Hip weapon 30101", "Secondary 10004", "Hip weapon 30103"};
    const char* weapon_values[] = {"EQP_WP_10101", "EQP_WP_30101", "EQP_WP_10004", "EQP_WP_30103"};
    const char* slots[] = {"secondary", "hip", "back"};
    ImGui::SetNextItemWidth(210); ImGui::Combo("Weapon preset", &weapon, weapon_names, ARRAYSIZE(weapon_names));
    ImGui::SetNextItemWidth(210); ImGui::Combo("Weapon slot", &slot, slots, ARRAYSIZE(slots));
    if (ImGui::Button("Refill resident preset")) {
        char command[96]{}; StringCchPrintfA(command, ARRAYSIZE(command), "weapon %s %s", slots[slot], weapon_values[weapon]); Send(command);
    }
    ImGui::TextWrapped("Weapon and equipment changes affect your local player. The partner motion mapper does not synchronize weapons.");
    static char equipment[64] = "EQP_WP_10101";
    static int payload = 1;
    ImGui::SetNextItemWidth(280); ImGui::InputText("Equipped item", equipment, ARRAYSIZE(equipment));
    ImGui::SetNextItemWidth(220); ImGui::SliderInt("Dropped item payload", &payload, 1, 10);
    if (ImGui::Button("Drop equipped item")) {
        char command[100]{}; StringCchPrintfA(command, ARRAYSIZE(command), "drop_item %s %d", equipment, payload); Send(command);
    }
    const char* drop_slots[] = {"current_hip", "current_back", "current_secondary"};
    const char* drop_labels[] = {"Drop hip gun", "Drop back gun", "Drop secondary gun"};
    for (unsigned i = 0; i < ARRAYSIZE(drop_slots); ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(drop_labels[i])) {
            char command[64]{}; StringCchPrintfA(command, ARRAYSIZE(command), "drop_item %s %d", drop_slots[i], payload); Send(command);
        }
    }
    ImGui::TextWrapped("The item must already be equipped. Payload changes the dropped item's contents; it does not create multiple pickups.");
}
void Enemies(const debug_menu::Snapshot& s) noexcept {
    ImGui::TextUnformatted("Existing mission enemies / host mutation controls");
    Action("Read nearest enemy", "enemy_nearest");
    ImGui::BeginDisabled(!s.host);
    Action("Enable nearest enemy", "enemy_enable_nearest"); ImGui::SameLine();
    Action("Disable nearest enemy", "enemy_disable_nearest");
    Action("Kill nearest enemy", "enemy_kill_nearest");
    static int radius = 25;
    ImGui::SetNextItemWidth(230); ImGui::SliderInt("Clear radius (m)", &radius, 1, 100);
    if (ImGui::Button("Clear nearby enemies")) { char command[48]{}; StringCchPrintfA(command, ARRAYSIZE(command), "clear_enemies %d", radius); Send(command); }
    ImGui::TextUnformatted("Clear affects up to 16 readable soldiers per request.");
    ImGui::Separator();
    ImGui::TextWrapped("Enable or disable an existing mission enemy using its roster ID.");
    static unsigned id = 0;
    ImGui::SetNextItemWidth(250); ImGui::InputScalar("Roster ID", ImGuiDataType_U32, &id);
    ImGui::BeginDisabled(id == 0);
    if (ImGui::Button("Enable enemy")) { char command[64]{}; StringCchPrintfA(command, ARRAYSIZE(command), "enemy_enable %u", id); Send(command); }
    ImGui::SameLine();
    if (ImGui::Button("Disable enemy")) { char command[64]{}; StringCchPrintfA(command, ARRAYSIZE(command), "enemy_disable %u", id); Send(command); }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextWrapped("Arbitrary new soldier creation is unavailable. These controls use the mission's existing roster.");
}
void World(const debug_menu::Snapshot& s) noexcept {
    ImGui::Text("Configured world level %u / host controls", s.world_level);
    ImGui::BeginDisabled(!s.host);
    static int seconds = 12 * 3600, weather = 0;
    ImGui::SetNextItemWidth(220); ImGui::SliderInt("Time (seconds)", &seconds, 0, 86399);
    ImGui::Text("%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    if (ImGui::Button("Set time")) { char command[32]{}; StringCchPrintfA(command, ARRAYSIZE(command), "time %d", seconds); Send(command); }
    const char* values[] = {"sunny", "cloudy", "rainy", "sandstorm", "foggy", "auto"};
    ImGui::SetNextItemWidth(220); ImGui::Combo("Weather", &weather, values, ARRAYSIZE(values));
    if (ImGui::Button("Set weather")) { char command[40]{}; StringCchPrintfA(command, ARRAYSIZE(command), "weather %s", values[weather]); Send(command); }
    ImGui::EndDisabled();
    if (!s.host) ImGui::TextUnformatted("Use the host's menu to change the shared environment.");
}
void DrawMenu() noexcept {
    const auto s = debug_menu::GetSnapshot();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2((display.x < 800 ? display.x - 24 : 760), (display.y < 700 ? display.y - 24 : 650)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560, 500), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
    bool visible = true;
    if (!ImGui::Begin("MGS Co-op Debug  |  F9", &visible, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); if (!visible) ChangeOpen(false); return; }
    ImGui::Text("%s  /  mission %u  /  %s", s.role[0] ? s.role : "offline", s.mission, s.gameplay_ready ? "supported scene" : "waiting for supported scene");
    ImGui::TextUnformatted("Offline session / redirected co-op save");
    ImGui::Text("Configured world level: %u", s.world_level);
    ImGui::TextWrapped("Actions require on-foot gameplay; loading, demos and game-over refuse them.");
    ImGui::Separator();
    const float available_actions = ImGui::GetContentRegionAvail().y - 205.0f - ImGui::GetStyle().ItemSpacing.y * 2;
    const float action_height = available_actions < 40 ? 40 : available_actions;
    if (ImGui::BeginChild("actions", ImVec2(0, action_height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (ImGui::BeginTabBar("debug_tabs")) {
            struct Tab { const char* name; const char* child; void (*draw)(const debug_menu::Snapshot&) noexcept; };
            const Tab tabs[] = {{"Overview", "overview_actions", &Overview}, {"Movement / animations", "movement_actions", &Movement},
                {"Player / weapons", "player_actions", &Player}, {"Enemies", "enemy_actions", &Enemies}, {"World", "world_actions", &World}};
            for (const auto& tab : tabs) if (ImGui::BeginTabItem(tab.name)) {
                if (ImGui::BeginChild(tab.child, ImVec2(0, 0))) {
                    ImGui::BeginDisabled(!s.gameplay_ready || s.terminating);
                    tab.draw(s);
                    ImGui::EndDisabled();
                }
                ImGui::EndChild(); ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::BeginChild("results", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        // Read after button handling, so queue depth and results also reflect
        // commands submitted during this frame without persistent UI status.
        const auto result = debug_menu::GetSnapshot();
        footer_status = result.queue_depth ? "Queued. Waiting for the game update thread." :
            result.last_id || result.rejected ? "No commands waiting. Latest result below." : "Choose an action; its game-thread result appears below.";
        ImGui::TextUnformatted(footer_status);
        ImGui::Text("Pending %u   completed %u   refused %u", result.queue_depth, result.completed, result.rejected);
        if (ImGui::BeginChild("latest_result", ImVec2(0, ImGui::GetTextLineHeight() * 2 + ImGui::GetStyle().ItemSpacing.y), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) ImGui::TextWrapped("Latest: %s", result.last_result);
        const bool latest_hovered = ImGui::IsWindowHovered();
        ImGui::EndChild();
        if (latest_hovered && result.last_result[0]) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(600); ImGui::TextUnformatted(result.last_result); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
        if (ImGui::BeginChild("receipts", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
            for (const auto& entry : result.history) if (entry[0]) ImGui::TextUnformatted(entry);
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::End();
    if (!visible) ChangeOpen(false);
}
void Draw() noexcept {
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
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    if (!now_open) { ImGui::EndFrame(); return; }
    DrawMenu();
    ImGui::Render();
    ExtraState saved(context);
    context->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    saved.Restore(context);
    if (frames.fetch_add(1, std::memory_order_relaxed) == 0) Log("overlay=drawn; frame=1\r\n");
}
HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain, UINT interval, UINT flags) noexcept {
    presents.fetch_add(1, std::memory_order_relaxed);
    if ((flags & DXGI_PRESENT_TEST) == 0 && debug_menu::Enabled() && !resizing.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&render_lock);
        if (initialized && swapchain != chain) Cleanup();
        if (initialized || Initialize(chain)) {
            if (target && !resizing.load(std::memory_order_relaxed)) Draw();
        }
        ReleaseSRWLockExclusive(&render_lock);
    }
    // DXGI may wait on the window's message thread. Never hold our lock here.
    const HRESULT result = original_present(chain, interval, flags);
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        AcquireSRWLockExclusive(&render_lock);
        if (swapchain == chain) Cleanup();
        ReleaseSRWLockExclusive(&render_lock);
        Log("overlay=device_lost; resources=released\r\n");
    }
    return result;
}
HRESULT STDMETHODCALLTYPE Resize(IDXGISwapChain* chain, UINT buffers, UINT width, UINT height, DXGI_FORMAT format, UINT flags) noexcept {
    resizing.store(true, std::memory_order_release);
    AcquireSRWLockExclusive(&render_lock);
    const bool ours = initialized && swapchain == chain;
    if (ours) { Release(target); ImGui::SetCurrentContext(gui); ImGui_ImplDX11_InvalidateDeviceObjects(); }
    ReleaseSRWLockExclusive(&render_lock);
    const HRESULT result = original_resize(chain, buffers, width, height, format, flags);
    AcquireSRWLockExclusive(&render_lock);
    if (ours && initialized && swapchain == chain) CreateTarget(chain);
    ReleaseSRWLockExclusive(&render_lock);
    resizing.store(false, std::memory_order_release);
    resizes.fetch_add(1, std::memory_order_relaxed);
    Log(SUCCEEDED(result) ? "overlay=resize; result=ok\r\n" : "overlay=resize; result=failed\r\n");
    return result;
}
BOOL CALLBACK InstallOnce(PINIT_ONCE, PVOID, PVOID*) noexcept {
    if (!debug_menu::Enabled()) return TRUE;
    InitializeLog();
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) { Log("overlay=refused; reason=minhook\r\n"); return TRUE; }
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = DefWindowProcW; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"MGSCoopDX11Probe";
    const ATOM atom = RegisterClassExW(&cls);
    if (!atom) { Log("overlay=refused; reason=probe_class\r\n"); return TRUE; }
    const HWND hidden = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, cls.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow = hidden;
    desc.SampleDesc.Count = 1; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* dummy = nullptr; ID3D11Device* dummy_device = nullptr; ID3D11DeviceContext* dummy_context = nullptr;
    const HRESULT created = hidden ? D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &desc, &dummy, &dummy_device, nullptr, &dummy_context) : E_FAIL;
    bool ok = SUCCEEDED(created);
    if (ok) {
        void** methods = *reinterpret_cast<void***>(dummy);
        ok = AddHook(present_hook, methods[8], reinterpret_cast<void*>(&Present)) &&
             AddHook(resize_hook, methods[13], reinterpret_cast<void*>(&Resize));
        original_present = reinterpret_cast<PresentFn>(present_hook.original);
        original_resize = reinterpret_cast<ResizeFn>(resize_hook.original);
        if (ok) ok = InputHooks() && EnableHooks();
    }
    if (!ok) RemoveHooks();
    Release(dummy_context); Release(dummy_device); Release(dummy);
    if (hidden) DestroyWindow(hidden);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
    Log(ok ? "overlay=installed; api=dx11; toggle=F9; graphics=Present_ResizeBuffers\r\n" : "overlay=refused; reason=graphics_or_input_hook\r\n");
    return TRUE;
}
} // namespace

bool CapturingInput() noexcept {
    if (!debug_menu::Enabled() || !Foreground()) return false;
    if (open.load(std::memory_order_acquire)) return true;
    if (!releasing.load(std::memory_order_acquire)) return false;
    const int held[] = {VK_F9, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (const int key : held) if (GetAsyncKeyState(key) & 0x8000) return true;
    if (!PadsReleased()) return true;
    if (GetTickCount64() < release_after.load(std::memory_order_acquire)) return true;
    releasing.store(false, std::memory_order_release);
    return false;
}
void Install() noexcept {
    const DWORD error = GetLastError();
    if (debug_menu::Enabled()) InitOnceExecuteOnce(&install_once, &InstallOnce, nullptr, nullptr);
    SetLastError(error);
}
#ifdef MGS_DEBUG_OVERLAY_TESTING
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
const char* FooterStatusForTest() noexcept {
    AcquireSRWLockShared(&render_lock); const char* result = footer_status; ReleaseSRWLockShared(&render_lock); return result;
}
void ShutdownForTest() noexcept {
    AcquireSRWLockExclusive(&render_lock); Cleanup(); ReleaseSRWLockExclusive(&render_lock);
    RemoveHooks();
    RemoveHook(fixture_state); RemoveHook(fixture_data); RemoveHook(fixture_pad);
    if (log_file != INVALID_HANDLE_VALUE) CloseHandle(log_file);
    log_file = INVALID_HANDLE_VALUE;
}
#endif
} // namespace mgscoop::debug_overlay
