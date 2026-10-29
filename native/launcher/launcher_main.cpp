// Diamond Dogs Together: DiamondDogsTogether.exe, the Win32 + DX11 window shell
// around the ImGui pages (launcher_app.cpp). A WIN32-subsystem program: it
// never opens a console. Command-line modes (launcher_ui_logic.hpp):
//   (none)                     the window, on the real model (launcher core)
//   --mock                     the window on the simulated model (developer check)
//   --watch <session>          no window (the background helper, launcher core)
//   --selftest                 built-in checks, exit code 0 = passed
//   --screenshot-all <dir>     renders every page once, hidden, and saves PNGs
//   --write-session host|join  developer only: setup if needed, then the session
//                              files exactly as Host / Join, without Steam
//   --end-session              developer only: switch co-op off again
//   --after-update <pid>       the window after a self-update: waits for the
//                              old window <pid> to close, then opens
//   --update-feed / --update-now  developer only: update from a loopback test feed
// Rendering is event driven: a frame is drawn on input, and otherwise only on a
// timer (1 s when idle, 4-10 Hz while setup or a game runs).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "ddt_launcher_resource.h"
#include "imgui.h"
#include "launcher_app.hpp"
#include "launcher_entry.hpp"
#include "launcher_model_mock.hpp"
#include "launcher_theme.hpp"
#include "launcher_ui_logic.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
using ddt::ui::Options;

constexpr wchar_t kWindowClass[] = L"DiamondDogsTogetherWindow";
constexpr wchar_t kWindowTitle[] = L"Diamond Dogs Together";
constexpr wchar_t kFolderProp[] = L"ddt.folder";
constexpr int kClientWidth = 760;
constexpr int kClientHeight = 520;

template <typename T>
void Release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// ---- Text and paths --------------------------------------------------------
std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring ExeDir() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool Exists(const std::wstring& path, bool directory) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && ((a & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory;
}

// Console output for --help / --selftest when started from a terminal or ctest
// (inherited handles); a double-clicked exe has none and stays silent.
void Print(const std::wstring& text) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            h = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (!h || h == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0, written = 0;
    if (GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
    } else {
        const std::string utf8 = Utf8(text);
        WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

// ---- Graphics --------------------------------------------------------------
struct Gfx {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* logo = nullptr;
    UINT resize_w = 0, resize_h = 0;
    bool occluded = false;
};

void CreateTarget(Gfx& g) {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g.swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        g.device->CreateRenderTargetView(back, nullptr, &g.rtv);
        back->Release();
    }
}

bool CreateDevice(Gfx& g, HWND hwnd) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const D3D_DRIVER_TYPE drivers[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    const DXGI_SWAP_EFFECT effects[] = {DXGI_SWAP_EFFECT_FLIP_DISCARD, DXGI_SWAP_EFFECT_DISCARD};
    for (DXGI_SWAP_EFFECT effect : effects) {
        for (D3D_DRIVER_TYPE driver : drivers) {
            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 2;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferDesc.RefreshRate.Numerator = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd;
            sd.SampleDesc.Count = 1;
            sd.Windowed = TRUE;
            sd.SwapEffect = effect;
            D3D_FEATURE_LEVEL got{};
            const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 3, D3D11_SDK_VERSION, &sd,
                                                             &g.swap, &g.device, &got, &g.context);
            if (SUCCEEDED(hr)) {
                CreateTarget(g);
                return g.rtv != nullptr;
            }
        }
    }
    return false;
}

void DestroyDevice(Gfx& g) {
    Release(g.logo);
    Release(g.rtv);
    Release(g.swap);
    Release(g.context);
    Release(g.device);
}

IWICImagingFactory* g_wic = nullptr;
IWICImagingFactory* Wic() {
    if (!g_wic) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
    return g_wic;
}

// The emblem PNG (RCDATA) -> an RGBA texture for ImGui::Image.
ddt::LogoTexture LoadLogo(Gfx& g) {
    ddt::LogoTexture logo;
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(IDR_DDT_LOGO), reinterpret_cast<LPCWSTR>(RT_RCDATA));
    IWICImagingFactory* wic = Wic();
    if (!res || !wic) return logo;
    HGLOBAL mem = LoadResource(self, res);
    const void* bytes = mem ? LockResource(mem) : nullptr;
    const DWORD size = SizeofResource(self, res);
    if (!bytes || !size) return logo;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT w = 0, h = 0;
    std::vector<BYTE> pixels;
    if (SUCCEEDED(wic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(bytes)), size)) &&
        SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&w, &h)) && w && h && w <= 4096 && h <= 4096) {
        pixels.resize(static_cast<std::size_t>(w) * h * 4);
        if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) pixels.clear();
    }
    Release(converter);
    Release(frame);
    Release(decoder);
    Release(stream);
    if (pixels.empty()) return logo;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{pixels.data(), w * 4, 0};
    ID3D11Texture2D* texture = nullptr;
    if (SUCCEEDED(g.device->CreateTexture2D(&desc, &init, &texture)) && texture) {
        g.device->CreateShaderResourceView(texture, nullptr, &g.logo);
        texture->Release();
    }
    if (g.logo) {
        logo.id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(g.logo));
        logo.width = static_cast<float>(w);
        logo.height = static_cast<float>(h);
    }
    return logo;
}

// Reads the back buffer (after rendering, before Present) and saves a PNG.
bool SaveBackBufferPng(Gfx& g, const std::wstring& path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g.swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    bool ok = false;
    if (SUCCEEDED(g.device->CreateTexture2D(&desc, nullptr, &staging)) && staging) {
        g.context->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(g.context->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
            std::vector<BYTE> bgra(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
            for (UINT y = 0; y < desc.Height; ++y) {
                const BYTE* src = static_cast<const BYTE*>(map.pData) + static_cast<std::size_t>(y) * map.RowPitch;
                BYTE* dst = bgra.data() + static_cast<std::size_t>(y) * desc.Width * 4;
                for (UINT x = 0; x < desc.Width; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = 255;
                }
            }
            g.context->Unmap(staging, 0);
            IWICImagingFactory* wic = Wic();
            IWICStream* stream = nullptr;
            IWICBitmapEncoder* encoder = nullptr;
            IWICBitmapFrameEncode* frame = nullptr;
            IPropertyBag2* props = nullptr;
            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            ok = wic && SUCCEEDED(wic->CreateStream(&stream)) &&
                 SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
                 SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                 SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
                 SUCCEEDED(encoder->CreateNewFrame(&frame, &props)) && SUCCEEDED(frame->Initialize(props)) &&
                 SUCCEEDED(frame->SetSize(desc.Width, desc.Height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
                 IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
                 SUCCEEDED(frame->WritePixels(desc.Height, desc.Width * 4, static_cast<UINT>(bgra.size()), bgra.data())) &&
                 SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
            Release(props);
            Release(frame);
            Release(encoder);
            Release(stream);
        }
        staging->Release();
    }
    back->Release();
    return ok;
}

// ---- The window ------------------------------------------------------------
struct App {
    Gfx gfx;
    HWND hwnd = nullptr;
    float scale = 1.0f;
    bool forced_scale = false;
    float pending_scale = 0.0f;
    bool in_frame = false;
    bool ready = false;
    bool interactive = true;
    double last_input = -10.0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::unique_ptr<ddt::LauncherModel> model;
    ddt::MockModel* mock = nullptr;
    std::unique_ptr<ddt::LauncherUi> ui;
};
App* g_app = nullptr;

double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_app->start).count();
}

void RenderFrame(App& app, bool present, const std::wstring* capture_path = nullptr, bool* captured = nullptr) {
    if (app.in_frame || !app.ready) return;
    app.in_frame = true;
    if (app.pending_scale > 0.0f) {
        app.scale = app.pending_scale;
        app.pending_scale = 0.0f;
        ddt::theme::Apply(app.scale);
    }
    Gfx& g = app.gfx;
    if (g.resize_w && g.resize_h) {
        Release(g.rtv);
        g.swap->ResizeBuffers(0, g.resize_w, g.resize_h, DXGI_FORMAT_UNKNOWN, 0);
        g.resize_w = g.resize_h = 0;
        CreateTarget(g);
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    // The hidden screenshot run renders frames back to back: give each one a
    // normal frame time so fades (the modal dimming) settle within the run.
    if (capture_path || !app.interactive) ImGui::GetIO().DeltaTime = 0.1f;
    ImGui::NewFrame();
    app.ui->Frame(Now());
    ImGui::Render();
    const ImVec4 bg = ImGui::ColorConvertU32ToFloat4(ddt::theme::col::kWindow);
    const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
    g.context->OMSetRenderTargets(1, &g.rtv, nullptr);
    g.context->ClearRenderTargetView(g.rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (capture_path && captured) *captured = SaveBackBufferPng(g, *capture_path);
    if (present) {
        const HRESULT hr = g.swap->Present(1, 0);
        g.occluded = hr == DXGI_STATUS_OCCLUDED;
    }
    app.in_frame = false;
}

SIZE WindowSizeFor(float scale, UINT dpi, DWORD style) {
    RECT rc{0, 0, static_cast<LONG>(kClientWidth * scale + 0.5f), static_cast<LONG>(kClientHeight * scale + 0.5f)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
    return SIZE{rc.right - rc.left, rc.bottom - rc.top};
}

bool IsInputMessage(UINT msg) {
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
           msg == WM_MOUSELEAVE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ACTIVATEAPP ||
           msg == WM_SIZE || msg == WM_DPICHANGED;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 1;
    App* app = g_app;
    if (app && IsInputMessage(msg)) app->last_input = Now();
    switch (msg) {
    case WM_SIZE:
        if (app && wParam != SIZE_MINIMIZED) {
            app->gfx.resize_w = LOWORD(lParam);
            app->gfx.resize_h = HIWORD(lParam);
            RenderFrame(*app, true);   // keeps the content live while the frame is dragged
        }
        return 0;
    case WM_GETMINMAXINFO:
        if (app) {
            const UINT dpi = GetDpiForWindow(hwnd);
            const SIZE min = WindowSizeFor(app->scale, dpi ? dpi : 96, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)));
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = min.cx;
            info->ptMinTrackSize.y = min.cy;
        }
        return 0;
    case WM_DPICHANGED:
        if (app && !app->forced_scale) {
            app->pending_scale = HIWORD(wParam) / 96.0f;
            app->scale = app->pending_scale;   // the min size follows at once
            const RECT* r = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;   // no Alt menu
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void StyleTitleBar(HWND hwnd) {
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x10, 0x11, 0x13);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR (Windows 11) */, &caption, sizeof(caption));
    const COLORREF border = RGB(0x2F, 0x33, 0x39);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR (Windows 11) */, &border, sizeof(border));
}

// ---- Shell services --------------------------------------------------------
ddt::ShellServices MakeServices(bool interactive) {
    ddt::ShellServices s;
    if (!interactive) return s;   // the screenshot driver never opens Explorer or dialogs
    s.open_url = [](const std::string& url) {
        if (!url.empty()) ShellExecuteW(g_app ? g_app->hwnd : nullptr, L"open", Wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    s.open_folder = [](const std::string& folder) {
        const std::wstring w = Wide(folder);
        if (Exists(w, true)) ShellExecuteW(g_app->hwnd, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    s.reveal_file = [](const std::string& file) {
        const std::wstring w = Wide(file);
        if (Exists(w, false)) {
            const std::wstring args = L"/select,\"" + w + L"\"";
            ShellExecuteW(g_app->hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            return;
        }
        const std::size_t slash = w.find_last_of(L"\\/");
        if (slash != std::wstring::npos && Exists(w.substr(0, slash), true))
            ShellExecuteW(g_app->hwnd, L"open", w.substr(0, slash).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };
    s.pick_save_zip = [](std::string& out) {
        wchar_t file[MAX_PATH * 4] = {};
        std::wstring desktop;
        PWSTR known = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &known)) && known) desktop = known;
        CoTaskMemFree(known);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_app->hwnd;
        ofn.lpstrFilter = L"Co-op save (*.zip)\0*.zip\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
        ofn.lpstrInitialDir = desktop.empty() ? nullptr : desktop.c_str();
        ofn.lpstrTitle = L"Load your partner's co-op save";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_HIDEREADONLY;
        if (!GetOpenFileNameW(&ofn)) return false;
        out = Utf8(file);
        return true;
    };
    return s;
}

// ---- Startup / shutdown ----------------------------------------------------
bool InitWindow(App& app, const Options& o, bool visible) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_DDT_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_DDT_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x17, 0x19, 0x1C));
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return false;

    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    UINT dpi = static_cast<UINT>(ImGui_ImplWin32_GetDpiScaleForMonitor(monitor) * 96.0f + 0.5f);
    if (!dpi) dpi = 96;
    app.forced_scale = o.scale > 0.0f;
    app.scale = app.forced_scale ? o.scale : dpi / 96.0f;
    const DWORD style = WS_OVERLAPPEDWINDOW;
    const SIZE size = WindowSizeFor(app.scale, dpi, style);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);
    const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - size.cx) / 2;
    const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - size.cy) / 2;
    app.hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle, style, visible ? x : -32000, visible ? y : -32000, size.cx,
                               size.cy, nullptr, nullptr, inst, nullptr);
    if (!app.hwnd) return false;
    StyleTitleBar(app.hwnd);
    if (!CreateDevice(app.gfx, app.hwnd)) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigInputTextCursorBlink = false;   // a steady caret: no timer redraws while typing
    // Never show ImGui's own error tooltip to players; the hidden screenshot run
    // logs recoverable errors to stdout, and its ctest fails on them.
    io.ConfigErrorRecoveryEnableTooltip = false;
    io.ConfigErrorRecoveryEnableDebugLog = !visible;
    ddt::theme::LoadFonts();
    ddt::theme::Apply(app.scale);
    ImGui_ImplWin32_Init(app.hwnd);
    ImGui_ImplDX11_Init(app.gfx.device, app.gfx.context);
    return true;
}

void Shutdown(App& app) {
    app.ready = false;
    app.ui.reset();
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    DestroyDevice(app.gfx);
    if (app.hwnd) DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

std::unique_ptr<ddt::MockModel> MakeMock(const Options& o, std::wstring& error) {
    ddt::MockOptions mo;
    mo.speed = o.mock_speed;
    if (!o.scenario.empty() && !ddt::ParseMockScenario(Utf8(o.scenario), mo.scenario)) {
        error = L"Unknown --mock-scenario: " + o.scenario;
        return nullptr;
    }
    return std::make_unique<ddt::MockModel>(mo);
}

// One launcher window per game folder: a second start brings the first to the front.
std::uint64_t FolderHash() {
    std::wstring dir = ExeDir();
    CharUpperBuffW(dir.data(), static_cast<DWORD>(dir.size()));
    std::uint64_t h = 1469598103934665603ull;
    for (wchar_t c : dir) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

// wait_ms > 0 (the restart after an update): wait that long for the window that
// holds the folder to release it before bringing it to the front instead.
HANDLE ClaimSingleInstance(std::uint64_t hash, int wait_ms) {
    wchar_t name[96];
    swprintf_s(name, L"Local\\DiamondDogsTogether-ui-%016llx", static_cast<unsigned long long>(hash));
    HANDLE mutex = CreateMutexW(nullptr, TRUE, name);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (wait_ms > 0) {
            // Released (or abandoned by a process that exited): this window owns the folder now.
            const DWORD r = WaitForSingleObject(mutex, static_cast<DWORD>(wait_ms));
            if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) return mutex;
        }
        struct Find {
            std::uint64_t hash;
            HWND found;
        } find{hash, nullptr};
        EnumWindows(
            [](HWND w, LPARAM p) -> BOOL {
                auto* f = reinterpret_cast<Find*>(p);
                wchar_t cls[64]{};
                GetClassNameW(w, cls, 64);
                if (wcscmp(cls, kWindowClass) == 0 &&
                    reinterpret_cast<std::uintptr_t>(GetPropW(w, kFolderProp)) == static_cast<std::uintptr_t>(f->hash)) {
                    f->found = w;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&find));
        if (find.found) {
            if (IsIconic(find.found)) ShowWindow(find.found, SW_RESTORE);
            SetForegroundWindow(find.found);
        }
        CloseHandle(mutex);
        return nullptr;
    }
    return mutex;
}

int RunWindow(const Options& o) {
    std::wstring error;
    // The real model (wp/launcher-core) by default; --mock keeps the simulated one.
    std::unique_ptr<ddt::LauncherModel> model;
    ddt::MockModel* mock_ptr = nullptr;
    if (o.mock) {
        std::unique_ptr<ddt::MockModel> mock = MakeMock(o, error);
        if (!mock) {
            MessageBoxW(nullptr, error.c_str(), kWindowTitle, MB_OK | MB_ICONWARNING);
            return 2;
        }
        mock_ptr = mock.get();
        model = std::move(mock);
    }
    const std::uint64_t hash = FolderHash();
    // After a self-update this window waits for the old one to close first
    // (core_update.hpp "Restart after an update"); an updater before 0.14.4
    // left DiamondDogsTogether.exe.old and exits by itself a moment later.
    const int claim_wait = ddt::ui::InstanceClaimWaitMs(o.after_update, Exists(ExeDir() + L"\\DiamondDogsTogether.exe.old", false));
    HANDLE instance = nullptr;
    if (o.after_update && !o.mock) {
        ddt::TakeOverAfterUpdate(o.after_update, [&](int ms) {
            instance = ClaimSingleInstance(hash, ms > claim_wait ? ms : claim_wait);
            return instance != nullptr;
        });
    } else {
        instance = ClaimSingleInstance(hash, o.mock ? 0 : claim_wait);
    }
    if (!instance) return 0;
    // After the single-instance claim: the real model reads (and tidies) the folder.
    if (!model) {
        ddt::ModelStart start;
        start.after_update = o.after_update;
        start.update_feed = Utf8(o.update_feed);
        start.update_now = o.update_now;
        model = ddt::MakeRealModel(ExeDir(), start);
    }
    App app;
    g_app = &app;
    if (!InitWindow(app, o, true)) {
        MessageBoxW(nullptr, L"Diamond Dogs Together could not open its window (Direct3D 11 is not available).",
                    kWindowTitle, MB_OK | MB_ICONERROR);
        Shutdown(app);
        ReleaseMutex(instance);
        CloseHandle(instance);
        return 3;
    }
    SetPropW(app.hwnd, kFolderProp, reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(hash)));
    app.mock = mock_ptr;
    app.model = std::move(model);
    app.ui = std::make_unique<ddt::LauncherUi>(*app.model, MakeServices(true), LoadLogo(app.gfx));
    app.ready = true;
    app.model->tick();
    RenderFrame(app, true);
    ShowWindow(app.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(app.hwnd);

    bool done = false;
    while (!done) {
        const bool recent_input = Now() - app.last_input < 0.6;
        int wait = ddt::ui::IdleWaitMs(app.model->phase(), recent_input);
        if (!recent_input && app.ui->Animating() && wait > 100) wait = 100;
        if (app.gfx.occluded || IsIconic(app.hwnd)) wait = 250;
        MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(wait), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done) break;
        app.model->tick();
        // The updated launcher runs and waits for this window: close it.
        if (ddt::ui::CloseForUpdate(app.model->update_info())) break;
        if (IsIconic(app.hwnd)) continue;
        if (app.gfx.occluded) {
            if (app.gfx.swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) continue;
            app.gfx.occluded = false;
        }
        RenderFrame(app, true);
    }
    Shutdown(app);
    app.model.reset();   // its threads end before the folder is handed over
    g_app = nullptr;
    ReleaseMutex(instance);
    CloseHandle(instance);
    return 0;
}

int RunScreenshots(const Options& o) {
    std::wstring error;
    std::unique_ptr<ddt::MockModel> mock = MakeMock(o, error);
    if (!mock) {
        Print(error + L"\n");
        return 2;
    }
    const std::wstring dir = o.screenshot_dir;
    if (!Exists(dir, true) && SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr) != ERROR_SUCCESS && !Exists(dir, true)) {
        Print(L"Cannot create " + dir + L"\n");
        return 2;
    }
    Options shot = o;
    if (shot.scale <= 0.0f) shot.scale = 1.0f;
    App app;
    g_app = &app;
    if (!InitWindow(app, shot, false)) {
        Print(L"Direct3D 11 is not available.\n");
        Shutdown(app);
        return 3;
    }
    ddt::MockModel& m = *mock;
    app.interactive = false;
    app.mock = mock.get();
    app.model = std::move(mock);
    app.ui = std::make_unique<ddt::LauncherUi>(*app.model, MakeServices(false), LoadLogo(app.gfx));
    app.ready = true;
    int failures = 0;
    for (int i = 0; i < ddt::LauncherUi::SceneCount(); ++i) {
        app.ui->PrepareScene(i, m);
        const std::string name = ddt::LauncherUi::SceneName(i);
        const std::wstring path = dir + L"\\" + Wide(name) + L".png";
        Print(L"scene " + Wide(name) + L"\n");
        bool saved = false;
        for (int frame = 0; frame < 5; ++frame) RenderFrame(app, false, frame == 4 ? &path : nullptr, &saved);
        if (!saved) {
            ++failures;
            Print(L"FAIL: " + path + L"\n");
        }
    }
    Print(std::to_wstring(ddt::LauncherUi::SceneCount() - failures) + L" screenshots written to " + dir + L"\n");
    Shutdown(app);
    g_app = nullptr;
    return failures ? 1 : 0;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; argv && i < argc; ++i) args.emplace_back(argv[i]);
    if (argv) LocalFree(argv);
    const Options o = ddt::ui::ParseCommandLine(args);
    if (!o.error.empty()) {
        Print(o.error + L"\n" + ddt::ui::UsageText());
        if (o.mode == Options::Mode::gui && !GetStdHandle(STD_OUTPUT_HANDLE))
            MessageBoxW(nullptr, (o.error + L"\n\n" + ddt::ui::UsageText()).c_str(), kWindowTitle, MB_OK | MB_ICONWARNING);
        return 2;
    }
    switch (o.mode) {
    case Options::Mode::help: Print(ddt::ui::UsageText()); return 0;
    case Options::Mode::watch: return ddt::RunWatch(o.watch_session);
    case Options::Mode::write_session: {
        std::string report;
        const int code = ddt::RunWriteSession(o.game_dir, o.write_host, o.write_code, o.write_character, report);
        Print(Wide(report));
        return code;
    }
    case Options::Mode::end_session: {
        std::string report;
        const int code = ddt::RunEndSession(o.game_dir, report);
        Print(Wide(report));
        return code;
    }
    case Options::Mode::selftest: {
        std::string report;
        const int code = ddt::RunSelfTest(report);
        Print(Wide(report));
        return code;
    }
    case Options::Mode::screenshots:
    case Options::Mode::gui: break;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int code = o.mode == Options::Mode::screenshots ? RunScreenshots(o) : RunWindow(o);
    Release(g_wic);
    CoUninitialize();
    return code;
}
