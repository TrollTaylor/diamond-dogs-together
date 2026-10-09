// Original private-desktop DX11 qualification fixture. GUI subsystem: no console.
// Never calls SwitchDesktop, foreground/focus APIs, or system input APIs.
#include "worker_win32.hpp"
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <sstream>

namespace {
using namespace mgsworker;
static_assert(sizeof(void*) == 8, "Build the worker probe for x64");
template<class T> struct Com {
    T* value = nullptr;
    ~Com() { if (value) value->Release(); }
    T** Address() { Require(value == nullptr, "com_output_not_empty"); return &value; }
};
LRESULT CALLBACK Window(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
struct WindowLease {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HWND hwnd = nullptr; ATOM klass = 0;
    ~WindowLease() { if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd); if (klass) UnregisterClassW(L"MGSWorkerDX11Probe", instance); }
    void Create(const std::string& desktop) {
        PrivateThread(desktop); WNDCLASSW wc{}; wc.lpfnWndProc = Window; wc.hInstance = instance; wc.lpszClassName = L"MGSWorkerDX11Probe";
        klass = RegisterClassW(&wc); Require(klass != 0, "probe_class");
        hwnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Private DX11 qualification", WS_OVERLAPPEDWINDOW,
                               0, 0, 160, 120, nullptr, nullptr, instance, nullptr);
        Require(hwnd != nullptr, "probe_window"); DWORD pid = 0;
        Require(GetWindowThreadProcessId(hwnd, &pid) == GetCurrentThreadId() && pid == GetCurrentProcessId(), "probe_window_owner");
        // Showing only this private desktop's window exercises a windowed swap chain;
        // it neither activates the window nor makes the desktop an input desktop.
        ShowWindow(hwnd, SW_SHOWNOACTIVATE); PrivateThread(desktop);
    }
};
void Le16(std::string& out, std::uint16_t n) { out += static_cast<char>(n & 255); out += static_cast<char>(n >> 8); }
void Le32(std::string& out, std::uint32_t n) { for (unsigned i = 0; i < 4; ++i) out += static_cast<char>((n >> (8 * i)) & 255); }
std::string Bitmap(const std::vector<unsigned char>& rgba) {
    constexpr std::uint32_t side = 64, pixels = side * side * 4, total = 54 + pixels;
    Require(rgba.size() == pixels, "probe_bitmap_pixels"); std::string out; out.reserve(total);
    Le16(out, 0x4d42); Le32(out, total); Le16(out, 0); Le16(out, 0); Le32(out, 54);
    Le32(out, 40); Le32(out, side); Le32(out, side); Le16(out, 1); Le16(out, 32);
    Le32(out, BI_RGB); Le32(out, pixels); Le32(out, 0); Le32(out, 0); Le32(out, 0); Le32(out, 0);
    for (int y = 63; y >= 0; --y) for (unsigned x = 0; x < side; ++x) {
        const auto at = (static_cast<unsigned>(y) * side + x) * 4;
        out += static_cast<char>(rgba[at + 2]); out += static_cast<char>(rgba[at + 1]); out += static_cast<char>(rgba[at]); out += static_cast<char>(rgba[at + 3]);
    }
    Require(out.size() == total, "probe_bitmap_extent"); return out;
}
struct Result {
    bool published = false; // Receipts emitted while the WindowLease and COM resources stay alive.
    bool ready = false, restriction_verified = false, creation_denied = false, hardware = false, pixels = false, present_tested = false, windowed = false;
    DWORD error = 0, denial_error = 0, pid = GetCurrentProcessId(), vendor = 0, device_id = 0, adapter_flags = 0;
    std::uint64_t creation = 0; std::string reason = "not_started", run, directory, desktop, initial_input, final_input, adapter, bmp_sha;
    HRESULT present = E_FAIL; unsigned feature = 0;
    std::string JsonText() const {
        std::ostringstream o;
        o << "{\"schema\":1,\"mode\":\"private_dx11_probe\",\"run_id\":" << Json(run) << ",\"pid\":" << pid
          << ",\"process_start_filetime\":" << Json(std::to_string(creation)) << ",\"desktop\":" << Json(desktop)
          << ",\"ready\":" << (ready ? "true" : "false") << ",\"reason\":" << Json(reason) << ",\"win32_error\":" << error
          << ",\"job_ui_restrictions_verified\":" << (restriction_verified ? "true" : "false") << ",\"desktop_creation_denied\":" << (creation_denied ? "true" : "false")
          << ",\"desktop_creation_error\":" << denial_error << ",\"switch_desktop_called\":false,\"switch_block_basis\":\"documented_JOB_OBJECT_UILIMIT_DESKTOP\""
          << ",\"system_input_called\":false,\"focus_called\":false,\"hardware_device\":" << (hardware ? "true" : "false")
          << ",\"adapter\":" << Json(adapter) << ",\"vendor_id\":" << vendor << ",\"device_id\":" << device_id << ",\"adapter_flags\":" << adapter_flags << ",\"feature_level\":" << feature
          << ",\"windowed\":" << (windowed ? "true" : "false") << ",\"present_tested\":" << (present_tested ? "true" : "false") << ",\"present_hresult\":" << Json(HResult(present))
          << ",\"present_occluded\":" << (present == DXGI_STATUS_OCCLUDED ? "true" : "false") << ",\"pixel_readback_verified\":" << (pixels ? "true" : "false")
          << ",\"width\":64,\"height\":64,\"rgba_expected\":[255,0,0,255],\"bitmap\":\"probe-backbuffer.bmp\",\"bitmap_sha256\":" << Json(bmp_sha)
          << ",\"input_desktop_initial\":" << Json(initial_input) << ",\"input_desktop_final\":" << Json(final_input)
          << ",\"input_desktop_unchanged_at_observations\":" << (!initial_input.empty() && final_input == initial_input ? "true" : "false")
          << ",\"gameplay_validated\":false,\"gpu_timeout_settings_changed\":false}\n"; return o.str();
    }
};
void Execute(Result& result) {
    result.run = Env(L"MGS_WORKER_RUN_ID"); result.directory = Env(L"MGS_WORKER_RUN_DIR"); result.desktop = Env(L"MGS_WORKER_DESKTOP");
    Require(RunId(result.run) && result.desktop == "MGSWorker_" + result.run && AbsolutePath(result.directory) && Env(L"MGS_WORKER_ROLE") == "probe", "probe_context");
    auto directory_handles = LockAncestors(result.directory, true); (void)directory_handles;
    const auto acknowledgement = Wide(result.directory + "\\probe.ack");
    Require(GetFileAttributesW(acknowledgement.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND,
            "probe_ack_preexisting");
    result.creation = CreationTime(GetCurrentProcess()); result.initial_input = InputDesktop(); PrivateThread(result.desktop);
    BOOL member = FALSE; Require(IsProcessInJob(GetCurrentProcess(), nullptr, &member) != FALSE && member, "probe_not_in_job");
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui{}; JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    Require(QueryInformationJobObject(nullptr, JobObjectBasicUIRestrictions, &ui, sizeof(ui), nullptr) != FALSE &&
            QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr) != FALSE, "probe_job_query");
    constexpr DWORD expected_ui = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS;
    Require((ui.UIRestrictionsClass & expected_ui) == expected_ui && (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE), "probe_job_policy");
    result.restriction_verified = true;
    // Never invoke SwitchDesktop as a test. The same documented job policy blocks
    // desktop creation; an unexpected success is invisible and causes refusal.
    SetLastError(ERROR_SUCCESS);
    const auto denied_name = Wide(result.desktop + "_denial");
    HDESK denied = CreateDesktopW(denied_name.c_str(), nullptr, nullptr, 0, DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS, nullptr);
    result.denial_error = GetLastError();
    if (denied) {
        const HDESK assigned = GetThreadDesktop(GetCurrentThreadId());
        if (assigned == denied) {
            OwnedDesktop original(OpenDesktopW(Wide(result.desktop).c_str(), 0, FALSE, DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS));
            if (original.value) SetThreadDesktop(original.value);
        }
        CloseDesktop(denied); throw Failure("probe_desktop_creation_unexpected_success", ERROR_SUCCESS);
    }
    Require(result.denial_error == ERROR_ACCESS_DENIED, "probe_desktop_creation_not_policy_denial"); result.creation_denied = true; PrivateThread(result.desktop);
    WindowLease window; window.Create(result.desktop);
    DXGI_SWAP_CHAIN_DESC swap{}; swap.BufferDesc.Width = 64; swap.BufferDesc.Height = 64; swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count = 1; swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; swap.BufferCount = 1; swap.OutputWindow = window.hwnd;
    swap.Windowed = TRUE; swap.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    Com<ID3D11Device> device; Com<ID3D11DeviceContext> context; Com<IDXGISwapChain> chain;
    D3D_FEATURE_LEVEL level{}; const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    const HRESULT created = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, 1, D3D11_SDK_VERSION, &swap, chain.Address(), device.Address(), &level, context.Address());
    Require(SUCCEEDED(created) && level >= D3D_FEATURE_LEVEL_11_0, "probe_hardware_dx11_device"); result.feature = level;
    Com<IDXGIDevice> dxgi; Com<IDXGIAdapter> adapter; Com<IDXGIAdapter1> adapter1;
    Require(SUCCEEDED(device.value->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(dxgi.Address()))) &&
            SUCCEEDED(dxgi.value->GetAdapter(adapter.Address())) &&
            SUCCEEDED(adapter.value->QueryInterface(__uuidof(IDXGIAdapter1), reinterpret_cast<void**>(adapter1.Address()))), "probe_adapter_identity");
    DXGI_ADAPTER_DESC1 description{}; Require(SUCCEEDED(adapter1.value->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE), "probe_software_adapter");
    result.adapter = Narrow(description.Description); result.vendor = description.VendorId; result.device_id = description.DeviceId; result.adapter_flags = description.Flags; result.hardware = true;
    BOOL fullscreen = TRUE; Require(SUCCEEDED(chain.value->GetFullscreenState(&fullscreen, nullptr)) && !fullscreen, "probe_swapchain_not_windowed"); result.windowed = true;
    Com<ID3D11Texture2D> back; Com<ID3D11RenderTargetView> target; Com<ID3D11Texture2D> staging;
    Require(SUCCEEDED(chain.value->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.Address()))) &&
            SUCCEEDED(device.value->CreateRenderTargetView(back.value, nullptr, target.Address())), "probe_backbuffer");
    D3D11_TEXTURE2D_DESC desc{}; back.value->GetDesc(&desc);
    Require(desc.Width == 64 && desc.Height == 64 && desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM && desc.MipLevels == 1 && desc.ArraySize == 1 && desc.SampleDesc.Count == 1, "probe_backbuffer_schema");
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    Require(SUCCEEDED(device.value->CreateTexture2D(&desc, nullptr, staging.Address())), "probe_staging_texture");
    const float red[4] = {1, 0, 0, 1}; std::vector<unsigned char> rgba(64 * 64 * 4);
    for (unsigned frame = 0; frame < 3; ++frame) {
        PrivateThread(result.desktop); MSG message{};
        for (unsigned queued = 0; queued < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++queued) { Require(message.message != WM_QUIT, "probe_window_closed"); TranslateMessage(&message); DispatchMessageW(&message); }
        context.value->ClearRenderTargetView(target.value, red); context.value->CopyResource(staging.value, back.value);
        D3D11_MAPPED_SUBRESOURCE mapped{}; Require(SUCCEEDED(context.value->Map(staging.value, 0, D3D11_MAP_READ, 0, &mapped)), "probe_staging_readback");
        const bool span_ok = mapped.pData && mapped.RowPitch >= 256 && mapped.RowPitch <= 1048576 / 64;
        if (span_ok) for (unsigned y = 0; y < 64; ++y) std::copy_n(static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, 256, rgba.data() + y * 256);
        context.value->Unmap(staging.value, 0); Require(span_ok, "probe_staging_span");
        for (std::size_t at = 0; at < rgba.size(); at += 4) Require(rgba[at] == 255 && rgba[at + 1] == 0 && rgba[at + 2] == 0 && rgba[at + 3] == 255, "probe_pixel_mismatch");
        result.present = chain.value->Present(0, 0); result.present_tested = true;
        Require(result.present == S_OK || result.present == DXGI_STATUS_OCCLUDED, "probe_present_failure"); PrivateThread(result.desktop);
    }
    result.pixels = true;
    const auto bitmap = Wide(result.directory + "\\probe-backbuffer.bmp"); Atomic(bitmap, Bitmap(rgba), false);
    auto file = OpenRead(bitmap); result.bmp_sha = Hash(file.value); result.final_input = InputDesktop(); PrivateThread(result.desktop);
    Require(result.final_input == result.initial_input, "probe_input_desktop_changed"); result.ready = true; result.reason = "hardware_pixels_private_desktop";
    // Publish before these local WindowLease and COM objects unwind. The
    // supervisor inspects a live stable HWND, then explicitly releases us.
    Atomic(Wide(result.directory + "\\probe.json"), result.JsonText(), false); result.published = true;
    Atomic(Wide(result.directory + "\\probe.context"), "MGSWORKERPROBE1 " + result.run + " probe " + std::to_string(result.pid) + " " +
        std::to_string(result.creation) + " " + result.desktop + " admitted\n", false);
    const auto release_deadline = GetTickCount64() + 10000;
    for (;;) {
        if (GetTickCount64() >= release_deadline) throw Failure("probe_ack_timeout", WAIT_TIMEOUT);
        PrivateThread(result.desktop); result.final_input = InputDesktop();
        Require(result.final_input == result.initial_input, "probe_input_desktop_changed");
        DWORD owner_pid = 0;
        Require(IsWindow(window.hwnd) && GetWindowThreadProcessId(window.hwnd, &owner_pid) == GetCurrentThreadId() &&
                owner_pid == result.pid, "probe_window_owner_before_release");
        MSG message{};
        for (unsigned queued = 0; queued < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++queued) {
            Require(message.message != WM_QUIT, "probe_window_closed"); TranslateMessage(&message); DispatchMessageW(&message);
        }
        std::string text;
        // OpenRead rejects reparse points/final-path mismatches; the existing
        // directory_handles pins every ancestor until this wait finishes.
        if (TryText(acknowledgement, 256, text)) {
            if (GetTickCount64() >= release_deadline) throw Failure("probe_ack_timeout", WAIT_TIMEOUT);
            Require(IsWindow(window.hwnd) && GetWindowThreadProcessId(window.hwnd, &owner_pid) == GetCurrentThreadId() &&
                    owner_pid == result.pid, "probe_window_owner_before_release");
            Require(ProbeRelease(text, result.run, result.pid, result.creation), "probe_ack_ownership_mismatch");
            return; // Deliberate teardown after the exact supervisor release.
        }
        Sleep(10);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    Result result;
    try { int count = 0; auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
        const bool valid = args && count == 2 && std::wstring(args[1]) == L"--probe"; if (args) LocalFree(args);
        Require(valid, "probe_arguments"); Execute(result);
    } catch (const Failure& e) { result.ready = false; result.reason = e.what(); result.error = e.code; }
      catch (...) { result.ready = false; result.reason = "probe_exception"; result.error = ERROR_UNHANDLED_EXCEPTION; }
    if ((!result.published || !result.ready) && AbsolutePath(result.directory) && RunId(result.run)) {
        try { auto held = LockAncestors(result.directory, true); (void)held;
            // Failure replaces only this probe's already-published result.
            // Success never duplicates CREATE_NEW ready/context writes.
            Atomic(Wide(result.directory + "\\probe.json"), result.JsonText(), result.published);
        } catch (...) { return 3; }
    }
    return result.ready ? 0 : 2;
}
