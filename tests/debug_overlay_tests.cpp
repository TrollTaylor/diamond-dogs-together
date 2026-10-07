#include "debug_overlay_fixture.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <array>
#include <cstdio>
#include <cstring>
#include "imgui_internal.h"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_overlay.hpp"

namespace {
unsigned checks = 0, failures = 0, window_keys = 0;
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT id, WPARAM wp, LPARAM lp) {
    if (id == WM_KEYDOWN) ++window_keys;
    return DefWindowProcW(hwnd, id, wp, lp);
}
template<class T> void Release(T*& value) { if (value) value->Release(); value = nullptr; }
void Pump() { MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
ImGuiWindow* Child(const char* name) {
    for (auto* value : ImGui::GetCurrentContext()->Windows) if (std::strstr(value->Name, name)) return value;
    return nullptr;
}
}
int main() {
    namespace ui = mgscoop::debug_overlay;
    namespace bridge = mgscoop::debug_menu;
    bridge::Configure(false, true, true);
    ui::Install();
    Check(!ui::StatsForTest().initialized && !ui::CapturingInput(), "disabled install is inert");
    WNDCLASSEXW cls{sizeof(cls)};
    cls.lpfnWndProc = WndProc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"MGSCoopOverlayFixture";
    Check(RegisterClassExW(&cls) != 0, "register hidden fixture");
    const HWND hwnd = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1024, 768, nullptr, nullptr, cls.hInstance, nullptr);
    Check(hwnd != nullptr && !IsWindowVisible(hwnd), "fixture never shows or steals focus");
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT; desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1; desc.Windowed = TRUE;
    IDXGISwapChain* chain = nullptr; ID3D11Device* device = nullptr; ID3D11DeviceContext* ctx = nullptr;
    const HRESULT created = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &ctx);
    Check(SUCCEEDED(created), "hardware DX11 fixture creation");
    if (FAILED(created)) return 1;
    bridge::Configure(true, true, true); bridge::BeginMission(30010);
    bridge::Snapshot published{}; published.stage = 3; published.world_level = 3; published.pose_valid = true; published.gameplay_ready = true;
    strcpy_s(published.apply, "xform"); strcpy_s(published.dm, "map"); bridge::Publish(published);
    ui::Install();
    Check(SUCCEEDED(chain->Present(0, DXGI_PRESENT_TEST)), "test Present forwards");
    Check(!ui::StatsForTest().initialized, "DXGI_PRESENT_TEST skips initialization");
    Check(SUCCEEDED(chain->Present(0, 0)), "real Present forwards");
    Check(ui::StatsForTest().initialized && ui::StatsForTest().frames == 0, "first Present initializes closed menu");
    Check(!ui::CapturingInput(), "closed menu passes input");

    std::array<void*, 16> methods{};
    methods[9] = reinterpret_cast<void*>(&DebugFixtureState); methods[10] = reinterpret_cast<void*>(&DebugFixtureData);
    methods[15] = reinterpret_cast<void*>(&DebugFixtureInfo);
    DebugInputFixture fixture{methods.data()};
    auto* object = reinterpret_cast<IDirectInputDevice8W*>(&fixture);
    Check(ui::HookInputFixtureForTest(methods[9], methods[10]), "real MinHook input fixture installation");
    std::array<unsigned char, 256> keys{};
    Check(SUCCEEDED(DebugFixtureState(object, static_cast<DWORD>(keys.size()), keys.data())) && keys[0] == 0x80, "closed keyboard forwards original state");
    Check(GetLastError() == 0x12345678, "closed keyboard preserves original last error");
    DIDEVICEOBJECTDATA event{}; DWORD count = 1;
    Check(SUCCEEDED(DebugFixtureData(object, sizeof(event), &event, &count, DIGDD_PEEK)) && count == 1 && fixture.last_flags == DIGDD_PEEK, "closed buffered peek forwards unchanged");

    ui::SetForegroundForTest(false);
    SendMessageW(hwnd, WM_KEYDOWN, VK_F9, 0);
    SendMessageW(hwnd, WM_KEYUP, VK_F9, 0);
    Check(!ui::StatsForTest().open && !ui::CapturingInput(), "background F9 cannot open or capture unfocused game");
    Check(window_keys == 1, "background F9 forwards to original WndProc");
    window_keys = 0;
    ui::SetForegroundForTest(true);
    SendMessageW(hwnd, WM_KEYDOWN, VK_F9, 0);
    SendMessageW(hwnd, WM_KEYDOWN, VK_F9, 1LL << 30);
    Check(ui::StatsForTest().open && ui::CapturingInput(), "F9 opens immediately and autorepeat does not toggle");
    SendMessageW(hwnd, WM_KEYUP, VK_F9, 0);
    SendMessageW(hwnd, WM_KEYDOWN, 'W', 0);
    Check(window_keys == 0, "open WndProc captures keyboard before game");
    const unsigned old_characters = ui::StatsForTest().characters;
    ui::RawKeyForTest('1');
    Check(ui::StatsForTest().characters == old_characters, "legacy keyboard does not duplicate WM_CHAR");
    RAWINPUTDEVICE raw_keyboard{1, 6, RIDEV_NOLEGACY, hwnd};
    Check(RegisterRawInputDevices(&raw_keyboard, 1, sizeof(raw_keyboard)) != FALSE, "register fixture-only raw keyboard");
    ui::RawKeyForTest('1');
    Check(ui::StatsForTest().characters > old_characters, "raw no-legacy keyboard still supplies text to menu");
    raw_keyboard.dwFlags = RIDEV_REMOVE; raw_keyboard.hwndTarget = nullptr;
    Check(RegisterRawInputDevices(&raw_keyboard, 1, sizeof(raw_keyboard)) != FALSE, "remove fixture raw keyboard registration");
    Check(SUCCEEDED(DebugFixtureState(object, static_cast<DWORD>(keys.size()), keys.data())) && keys[0] == 0 && keys[255] == 0, "modal keyboard state neutral");
    Check(GetLastError() == 0x12345678, "modal state preserves original last error");
    DIMOUSESTATE2 mouse{};
    Check(SUCCEEDED(DebugFixtureState(object, sizeof(mouse), &mouse)) && mouse.lX == 0 && mouse.rgbButtons[7] == 0, "modal mouse state neutral");
    std::array<unsigned char, 32> unknown{};
    Check(SUCCEEDED(DebugFixtureState(object, static_cast<DWORD>(unknown.size()), unknown.data())) && unknown[0] == 0x80, "unknown DirectInput format unchanged");
    fixture.type = DI8DEVTYPE_JOYSTICK;
    Check(SUCCEEDED(DebugFixtureState(object, static_cast<DWORD>(keys.size()), keys.data())) && keys[0] == 0x80, "unrelated joystick with keyboard-sized custom format unchanged");
    fixture.type = DI8DEVTYPE_KEYBOARD;
    fixture.result = DIERR_INPUTLOST; keys.fill(0x42);
    Check(DebugFixtureState(object, static_cast<DWORD>(keys.size()), keys.data()) == DIERR_INPUTLOST && keys[0] == 0x42, "input-lost result and buffer preserved");
    fixture.result = DI_OK; count = 1;
    Check(SUCCEEDED(DebugFixtureData(object, sizeof(event), &event, &count, DIGDD_PEEK)) && count == 0 && fixture.flushes == 1 && fixture.last_flags == 0, "modal peek drains queued events and returns none");
    Check(GetLastError() == 0x12345678, "buffered input preserves first original last error");
    fixture.result = DIERR_INPUTLOST; count = 3;
    Check(DebugFixtureData(object, sizeof(event), &event, &count, DIGDD_PEEK) == DIERR_INPUTLOST && count == 3, "buffered failure unchanged");
    fixture.result = DI_OK;

    Check(ui::HookPadFixtureForTest(reinterpret_cast<void*>(&DebugFixturePad)), "real MinHook XInput fixture installation");
    auto& pad = debug_pad_fixture[0]; pad.result = ERROR_SUCCESS;
    pad.state.dwPacketNumber = 77; pad.state.Gamepad.bRightTrigger = 255;
    XINPUT_STATE state{};
    Check(DebugFixturePad(0, &state) == ERROR_SUCCESS && state.dwPacketNumber == 77 && state.Gamepad.bRightTrigger == 0, "modal XInput is connected neutral with original packet number");
    Check(GetLastError() == 0x87654321, "modal XInput preserves original last error");
    ui::SetOpenForTest(false); Sleep(150);
    Check(ui::CapturingInput() && DebugFixturePad(0, &state) == ERROR_SUCCESS && state.Gamepad.bRightTrigger == 0, "held trigger remains blocked beyond close delay");
    pad.state.Gamepad = {}; pad.state.Gamepad.wButtons = XINPUT_GAMEPAD_A;
    Check(ui::CapturingInput(), "held controller button prolongs release guard");
    pad.state.Gamepad = {}; pad.state.Gamepad.sThumbLX = 16000;
    Check(ui::CapturingInput(), "held controller stick prolongs release guard");
    pad.state.Gamepad = {};
    ui::SetMouseHeldForTest(true);
    Check(ui::CapturingInput(), "held mouse also prolongs release guard");
    ui::SetMouseHeldForTest(false);
    Check(!ui::CapturingInput() && DebugFixturePad(0, &state) == ERROR_SUCCESS && state.dwPacketNumber == 77, "controller release restores original polling");
    ui::SetOpenForTest(true); pad.result = ERROR_DEVICE_NOT_CONNECTED;
    state.dwPacketNumber = 42; state.Gamepad.wButtons = XINPUT_GAMEPAD_B;
    Check(DebugFixturePad(0, &state) == ERROR_DEVICE_NOT_CONNECTED && state.dwPacketNumber == 42 && state.Gamepad.wButtons == XINPUT_GAMEPAD_B, "disconnected controller error and output are unchanged");
    ui::SetOpenForTest(false); Sleep(150);
    Check(!ui::CapturingInput(), "disconnected controller releases capture");
    ui::SetOpenForTest(true);

    ID3D11Texture2D* textures[2]{}; ID3D11RenderTargetView* rtvs[2]{};
    D3D11_TEXTURE2D_DESC tex{}; tex.Width = 64; tex.Height = 64; tex.MipLevels = 1; tex.ArraySize = 1;
    tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM; tex.SampleDesc.Count = 1; tex.BindFlags = D3D11_BIND_RENDER_TARGET;
    for (unsigned i = 0; i < 2; ++i) {
        Check(SUCCEEDED(device->CreateTexture2D(&tex, nullptr, &textures[i])), "create original render target");
        Check(SUCCEEDED(device->CreateRenderTargetView(textures[i], nullptr, &rtvs[i])), "create original render view");
    }
    ID3D11Buffer* buffer = nullptr; ID3D11UnorderedAccessView* uav = nullptr;
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 64; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bd.StructureByteStride = 4; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    Check(SUCCEEDED(device->CreateBuffer(&bd, nullptr, &buffer)), "create original UAV buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 16; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_COUNTER;
    Check(SUCCEEDED(device->CreateUnorderedAccessView(buffer, &ud, &uav)), "create original UAV");
    ID3DBlob* code = nullptr; ID3DBlob* errors = nullptr; ID3D11ComputeShader* shader = nullptr;
    constexpr char source[] = "[numthreads(1,1,1)] void main(uint3 tid:SV_DispatchThreadID) {}";
    Check(SUCCEEDED(D3DCompile(source, sizeof(source), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &errors)), "compile original compute shader");
    if (code) Check(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader)), "create original compute shader");
    ctx->CSSetShader(shader, nullptr, 0);
    UINT counter = 7; ctx->OMSetRenderTargetsAndUnorderedAccessViews(2, rtvs, nullptr, 2, 1, &uav, &counter);
    ID3D11Texture2D* backbuffer = nullptr; ID3D11ShaderResourceView* backbuffer_input = nullptr;
    Check(SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer))), "obtain shader-readable fixture backbuffer");
    Check(SUCCEEDED(device->CreateShaderResourceView(backbuffer, nullptr, &backbuffer_input)), "create original backbuffer SRV");
    ctx->PSSetShaderResources(7, 1, &backbuffer_input);
    Check(SUCCEEDED(chain->Present(0, 0)) && ui::StatsForTest().frames == 1, "menu draws a real DX11 frame");
    ID3D11RenderTargetView* restored[2]{}; ctx->OMGetRenderTargets(2, restored, nullptr);
    Check(restored[0] == rtvs[0] && restored[1] == rtvs[1], "all original render targets restored");
    ID3D11UnorderedAccessView* restored_uav = nullptr; ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 2, 1, &restored_uav);
    Check(restored_uav == uav, "original output-merger UAV restored");
    ID3D11ComputeShader* restored_shader = nullptr; ctx->CSGetShader(&restored_shader, nullptr, nullptr);
    Check(restored_shader == shader, "original compute shader restored");
    ID3D11ShaderResourceView* restored_input = nullptr; ctx->PSGetShaderResources(7, 1, &restored_input);
    Check(restored_input == backbuffer_input, "implicit backbuffer SRV unbind restored outside ImGui slots");
    Release(restored_input);
    Release(restored[0]); Release(restored[1]); Release(restored_uav); Release(restored_shader);
    ctx->ClearState();
    Release(backbuffer_input); Release(backbuffer);
    auto* gui = ImGui::GetCurrentContext();
    Check(gui->TabBars.GetAliveCount() == 1, "debug tab bar is present outside action scrolling");
    if (gui->TabBars.GetAliveCount() == 1) {
        auto* bar = gui->TabBars.GetByIndex(0);
        for (auto& tab : bar->Tabs) if (std::strcmp(ImGui::TabBarGetTabName(bar, &tab), "Player / weapons") == 0) ImGui::TabBarQueueFocus(bar, &tab);
    }
    Check(SUCCEEDED(chain->Present(0, 0)) && SUCCEEDED(chain->Present(0, 0)), "render full Player tab layout");
    auto* main_window = ImGui::FindWindowByName("MGS Co-op Debug  |  F9");
    auto* player_actions = Child("/player_actions_");
    auto* results = Child("/results_"); auto* receipts = Child("/receipts_");
    Check(player_actions && player_actions->ScrollMax.y > 100, "long Player controls scroll within their bounded child");
    Check(main_window && main_window->ScrollMax.y == 0, "long Player controls do not scroll the menu header or footer");
    Check(main_window && results && results->OuterRectClipped.Max.y <= main_window->InnerClipRect.Max.y && results->Size.y >= 185 && results->Size.y <= 205,
        "fixed results panel remains visible inside menu viewport");
    Check(results && receipts && receipts->OuterRectClipped.Max.y <= results->InnerClipRect.Max.y && receipts->Size.y >= 90,
        "receipt history has its own visible footer area");
    Check(bridge::QueueCommand("position") && SUCCEEDED(chain->Present(0, 0)), "queue action and render immediate footer snapshot");
    Check(std::strncmp(ui::FooterStatusForTest(), "Queued.", 7) == 0, "footer reflects a waiting command");
    bridge::Command command{};
    Check(bridge::Pop(command), "natural-owner fixture consumes queued action");
    bridge::Result(command.id, true, "position=1,2,3; yaw=0");
    Check(SUCCEEDED(chain->Present(0, 0)) && std::strncmp(ui::FooterStatusForTest(), "No commands waiting.", 20) == 0,
        "footer clears queued status after a completed action");
    Check(SUCCEEDED(chain->ResizeBuffers(1, 800, 600, DXGI_FORMAT_R8G8B8A8_UNORM, 0)), "resize succeeds without leaked backbuffer reference");
    Check(ui::StatsForTest().resizes == 1 && SUCCEEDED(chain->Present(0, 0)) && ui::StatsForTest().frames == 6, "menu renders after resize");
    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
    Check(!ui::StatsForTest().open, "focus loss closes modal menu");
    // The guard is time/physical-release based. Wait only 150ms; no user input.
    Sleep(150);
    ui::SetOpenForTest(false);
    bridge::Configure(false, true, true);
    Check(!ui::CapturingInput(), "disabled bridge cannot capture");
    const auto old_frames = ui::StatsForTest().frames;
    Check(SUCCEEDED(chain->Present(0, 0)) && ui::StatsForTest().frames == old_frames, "disabled bridge cannot draw");
    ui::ShutdownForTest();
    Check(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)) == &WndProc, "original WndProc restored");
    Check(SUCCEEDED(chain->ResizeBuffers(1, 640, 480, DXGI_FORMAT_R8G8B8A8_UNORM, 0)), "resize after hook teardown");
    Check(SUCCEEDED(chain->Present(0, 0)), "Present after teardown forwards normally");
    for (unsigned i = 0; i < 2; ++i) { Release(rtvs[i]); Release(textures[i]); }
    Release(uav); Release(buffer); Release(shader); Release(code); Release(errors);
    Release(ctx); Release(device); Release(chain);
    DestroyWindow(hwnd); Pump(); UnregisterClassW(cls.lpszClassName, cls.hInstance);
    std::printf("debug_overlay_tests: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
