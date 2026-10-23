#include "debug_overlay_fixture.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "imgui_internal.h"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_overlay.hpp"
#include "mgscoop/debug_actions.hpp"
#include "mgscoop/debug_menu_ui.hpp"
#include "mgscoop/nametag.hpp"
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/save_handoff_state.hpp"

namespace mgscoop::debug_menu_ui {
// Test-only, defined in debug_menu_ui.cpp under MGS_DEBUG_OVERLAY_TESTING: the
// Partner / Co-op > Shared save section of the latest drawn frame.
bool SharedSaveStatusForTest(char* out, std::size_t capacity) noexcept;
bool SharedSaveItemForTest(int item, float (&rect)[4], bool& state) noexcept;
}  // namespace mgscoop::debug_menu_ui

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
    namespace mui = mgscoop::debug_menu_ui;
    namespace act = mgscoop::debug_actions;
    const auto present = [&](unsigned count) { bool ok = true; for (unsigned i = 0; i < count; ++i) ok = SUCCEEDED(chain->Present(0, 0)) && ok; return ok; };
    mui::SelectCategory(act::Category::player);
    Check(present(2), "render full Player page layout");
    auto* main_window = ImGui::FindWindowByName("MGS Co-op Debug | F9");
    auto* rail = Child("/rail_"); auto* page = Child("/page_"); auto* results = Child("/results_");
    Check(main_window && rail && page && results, "titled menu with rail, page and results children");
    Check(rail && rail->Size.x >= 148 && rail->Size.x <= 152, "category rail is 150 px");
    Check(page && page->ScrollMax.y > 100, "long Player page scrolls within its own child");
    Check(main_window && main_window->ScrollMax.y == 0, "header, quick bar and footer never scroll");
    Check(main_window && results && results->OuterRectClipped.Max.y <= main_window->InnerClipRect.Max.y && results->Size.y >= 20 && results->Size.y <= 60,
        "collapsed results footer stays visible inside the menu");
    Check(mui::RowDrawnForTest("player_teleport") && mui::RowDrawnForTest("player_heal"), "Player rows drawn");
    mui::SetFooterExpanded(true); Check(present(1), "expanded footer renders");
    results = Child("/results_");
    Check(results && results->Size.y <= 171 && results->OuterRectClipped.Max.y <= main_window->InnerClipRect.Max.y, "expanded footer bounded");
    mui::SetFooterExpanded(false);
    Check(mui::SetSearch("spawn") >= 3 && present(1), "search spawn renders");
    Check(mui::RowDrawnForTest("spawn_enemy_here") && mui::RowDrawnForTest("spawn_enemy_closest") && mui::RowDrawnForTest("spawn_enemy_route") &&
        !mui::RowDrawnForTest("player_heal"), "search shows the three spawn rows and nothing unrelated");
    mui::SetSearch("");
    char press[200]{};
    Check(mui::BeginPress("player_teleport") && present(3) && mui::TakePressResult(press, sizeof(press)) &&
        !std::strcmp(press, "disabled;action=player_teleport;reason=set_destination"), "Teleport is disabled until a destination is set");
    Check(mui::CurrentCategory() == act::Category::player, "press switches to the action's home page");
    const auto depth = bridge::GetSnapshot().queue_depth;
    Check(mui::BeginPress("enemy_clear") && present(3) && mui::TakePressResult(press, sizeof(press)) &&
        !std::strcmp(press, "needs_confirm;action=enemy_clear") && bridge::GetSnapshot().queue_depth == depth && mui::HasPendingConfirm(),
        "first press of a confirm action only opens the inline confirmation");
    Check(mui::CurrentCategory() == act::Category::enemies && mui::RowDrawnForTest("enemy_clear"), "press scrolls the Enemies page to the row");
    mui::Confirm(press, sizeof(press));
    Check(!std::strncmp(press, "confirmed;action=enemy_clear;queued_id=", 39) && bridge::GetSnapshot().queue_depth == depth + 1, "second step sends the command");
    bridge::Command queued{};
    Check(bridge::Pop(queued) && !std::strcmp(queued.text, "clear_enemies 25"), "confirmed command text");
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_R, true);
    Check(present(1), "Ctrl+R frame");
    io.AddKeyEvent(ImGuiKey_R, false); io.AddKeyEvent(ImGuiMod_Ctrl, false);
    Check(present(1) && mui::HasPendingConfirm() && bridge::GetSnapshot().queue_depth == depth, "Ctrl+R on a confirm action reopens the confirmation");
    mui::Cancel(press, sizeof(press)); Check(!std::strcmp(press, "cancelled") && !mui::HasPendingConfirm(), "cancel clears the confirmation");
    mui::SelectCategory(act::Category::player);
    Check(present(1), "Player page frame");
    io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_2, true);
    Check(present(1), "Ctrl+2 frame");
    io.AddKeyEvent(ImGuiKey_2, false); io.AddKeyEvent(ImGuiMod_Ctrl, false);
    Check(present(1) && mui::CurrentCategory() == act::Category::enemies, "Ctrl+2 selects Enemies");
    Check(mui::RowDrawnForTest("spawn_enemy_here") && mui::RowDrawnForTest("spawn_enemy_closest"), "Enemies opens at the top with Spawn enemy here");
    Check(mui::BeginPress("player_position") && present(3) && mui::TakePressResult(press, sizeof(press)) &&
        !std::strncmp(press, "pressed;action=player_position;command=position;queued_id=", 58), "press sends the row command");
    Check(bridge::GetSnapshot().queue_depth == depth + 1, "press queued one command");
    bridge::Command command{};
    Check(bridge::Pop(command), "natural-owner fixture consumes queued action");
    bridge::Result(command.id, 1, "position=1,2,3;yaw=0");
    Check(present(1) && bridge::GetSnapshot().receipts[0].id == command.id && bridge::GetSnapshot().receipts[0].status == 0,
        "result receipt reaches the footer source");
    // Partner / Co-op > Shared save (automatic save handoff; docs/debug-menu.md):
    // copied save_handoff_state only, UI-only rows, real mouse clicks.
    {
        namespace hs = mgscoop::save_handoff_state;
        using T = hs::Token;
        hs::ResetForTest();
        mui::SelectCategory(act::Category::partner);
        char shared[200]{};
        float rect[4]{};
        bool item_state = false;
        Check(present(2) && !mui::SharedSaveStatusForTest(shared, sizeof(shared)) && shared[0] == 0 &&
            !mui::SharedSaveItemForTest(0, rect, item_state) && !mui::SharedSaveItemForTest(1, rect, item_state),
            "Shared save is hidden while save handoff is not configured");
        const auto publish = [&](std::uint8_t role, T token, bool can_keep) {
            hs::View view{};
            view.available = true; view.role = role; view.token = static_cast<std::uint8_t>(token); view.pct = 42; view.why = 5;
            view.bytes = 608012; view.host_mission = 30010; view.can_keep = can_keep;
            hs::Publish(view);
        };
        const struct { std::uint8_t role; T token; bool keep; const char* line; } views[] = {
            {2, T::off, false, "Automatic save sharing is off for this session."},
            {2, T::idle, false, "Comparing your save with the host's; nothing to share yet."},
            {2, T::same, false, "Your save matches the host's place; nothing to share."},
            {2, T::wait_host, true, "Waiting for the host to reach Mother Base or free roam; their save is then copied to you."},
            {2, T::receiving, true, "Receiving the host's save: 42% of 594 KB."},
            {2, T::staged, true, "Host's save received and checked (594 KB)."},
            {2, T::countdown, true, "Host's save ready: MGSV restarts in 10 s. Your current co-op save is kept aside (Tools > Share save > Restore previous)."},
            {2, T::exiting, false, "MGSV is closing to load the host's save; Diamond Dogs Together starts it again (not back within a minute? Press Play in Steam)."},
            {2, T::kept, false, "You kept your own save for this session."},
            {2, T::refused, false, "The host's save cannot be shared: it continues into a mission co-op cannot join."},
            {2, T::failed, false, "Getting the host's save failed: the connection dropped. Nothing was changed."},
            {2, T::applied, false, "You now play on the host's save. Tools > Share save > Restore previous brings your previous co-op save back."},
            {1, T::idle, false, "Comparing your save with your partner's; nothing to share yet."},
            {1, T::offered, false, "Offering your save to your partner."},
            {1, T::sending, false, "Sending your save: 42%"},
            {1, T::partner_staged, false, "Your partner received your save."},
            {1, T::partner_restarting, false, "Your partner is restarting on your save."},
            {1, T::kept, false, "Your partner kept their own save."},
        };
        for (const auto& v : views) {
            publish(v.role, v.token, v.keep);
            const bool drawn = present(1) && mui::SharedSaveStatusForTest(shared, sizeof(shared));
            if (!drawn || std::strcmp(shared, v.line) != 0) std::printf("  shared save line: \"%s\"\n", shared);
            Check(drawn && std::strcmp(shared, v.line) == 0, "the Shared save section renders each token's status line");
            if (v.role == 2)
                Check(mui::SharedSaveItemForTest(0, rect, item_state) && item_state == v.keep && rect[2] > rect[0] && rect[3] > rect[1] &&
                    mui::SharedSaveItemForTest(1, rect, item_state) && item_state, "guest rows: Keep my own save (enabled iff can_keep) and Travel lock");
            else
                Check(!mui::SharedSaveItemForTest(0, rect, item_state) && !mui::SharedSaveItemForTest(1, rect, item_state),
                    "the host has no Keep my own save or Travel lock row");
        }
        // The countdown follows the newest G2 toast ("restarts in N s").
        publish(2, T::countdown, true);
        mgscoop::coop_hud::PostSaveHandoff(2, 7, 0);
        Check(present(1) && mui::SharedSaveStatusForTest(shared, sizeof(shared)) &&
            std::strcmp(shared, "Host's save ready: MGSV restarts in 7 s. Your current co-op save is kept aside (Tools > Share save > Restore previous).") == 0,
            "the countdown line follows the G2 toast");
        ImGuiIO& mio = ImGui::GetIO();
        const auto click = [&](const float (&r)[4]) {
            mio.AddMousePosEvent((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f);
            bool ok = present(1);
            mio.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            ok = present(1) && ok;
            mio.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            ok = present(1) && ok;
            mio.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            return present(1) && ok;
        };
        publish(2, T::same, false);
        Check(present(1) && mui::SharedSaveItemForTest(0, rect, item_state) && !item_state && click(rect) && !hs::TakeKeepOwnSave(),
            "a disabled Keep my own save requests nothing");
        publish(2, T::countdown, true);
        Check(present(1) && mui::SharedSaveItemForTest(0, rect, item_state) && item_state && click(rect), "click the enabled Keep my own save");
        Check(hs::TakeKeepOwnSave() && !hs::TakeKeepOwnSave(), "Keep my own save raises RequestKeepOwnSave exactly once");
        Check(!std::strcmp(bridge::GetSnapshot().last_ui_action, "handoff_keep"), "Keep my own save records handoff_keep");
        Check(!hs::TravelUnlocked() && mui::SharedSaveItemForTest(1, rect, item_state) && item_state, "the Travel lock starts checked");
        Check(click(rect) && hs::TravelUnlocked() && !std::strcmp(bridge::GetSnapshot().last_ui_action, "travel_unlock") &&
            mui::SharedSaveItemForTest(1, rect, item_state) && !item_state, "unchecking Travel lock unlocks travel (travel_unlock)");
        Check(click(rect) && !hs::TravelUnlocked() && !std::strcmp(bridge::GetSnapshot().last_ui_action, "travel_lock") &&
            mui::SharedSaveItemForTest(1, rect, item_state) && item_state, "checking Travel lock again locks travel (travel_lock)");
        Check(!hs::TakeKeepOwnSave(), "the Travel lock checkbox never requests Keep my own save");
        hs::ResetForTest();
        Check(present(1) && !mui::SharedSaveStatusForTest(shared, sizeof(shared)) && !mui::SharedSaveItemForTest(1, rect, item_state),
            "Shared save hides again when the view is not available");
        mui::SelectCategory(act::Category::player);
    }
    const auto frames_before_resize = ui::StatsForTest().frames;
    Check(SUCCEEDED(chain->ResizeBuffers(1, 800, 600, DXGI_FORMAT_R8G8B8A8_UNORM, 0)), "resize succeeds without leaked backbuffer reference");
    Check(ui::StatsForTest().resizes == 1 && present(1) && ui::StatsForTest().frames == frames_before_resize + 1, "menu renders after resize");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    Check(present(1) && !ui::StatsForTest().open, "Esc closes the menu");
    io.AddKeyEvent(ImGuiKey_Escape, false);
    Sleep(150);
    ui::SetOpenForTest(true); Check(present(1) && ui::StatsForTest().open, "menu reopens");
    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
    Check(!ui::StatsForTest().open, "focus loss closes modal menu");
    // The guard is time/physical-release based. Wait only 150ms; no user input.
    Sleep(150);
    ui::SetOpenForTest(false);
    // Partner name tag layer (docs/partner-nametag.md): closed menu, copied frames
    // only, drawn before Present; the fixture reads the backbuffer back.
    {
        namespace tag = mgscoop::nametag;
        ID3D11Texture2D* bb = nullptr; ID3D11RenderTargetView* bb_rtv = nullptr;
        Check(SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) &&
              SUCCEEDED(device->CreateRenderTargetView(bb, nullptr, &bb_rtv)), "fixture backbuffer view");
        Release(bb);
        const float dark[4] = {0.08f, 0.08f, 0.08f, 1.0f};
        const unsigned char* rgba = nullptr; unsigned width = 0, height = 0;
        const auto shot = [&]() {
            ctx->ClearRenderTargetView(bb_rtv, dark);
            ui::CaptureNextFrameForTest();
            return SUCCEEDED(chain->Present(0, 0)) && ui::CapturedFrameForTest(rgba, width, height) && width && height;
        };
        const auto lit = [&](float x0, float y0, float x1, float y1, bool green) {
            unsigned n = 0;
            for (int y = static_cast<int>(y0); y <= static_cast<int>(y1); ++y)
                for (int x = static_cast<int>(x0); x <= static_cast<int>(x1); ++x) {
                    if (x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height)) continue;
                    const unsigned char* px = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
                    if (green ? (px[1] >= 180 && px[0] <= 110 && px[2] <= 110) : (px[0] >= 200 && px[1] >= 200 && px[2] >= 200)) ++n;
                }
            return n;
        };
        const auto ring_pixels = [&](float cx, float cy) {
            unsigned n = 0;
            for (int y = static_cast<int>(cy) - 13; y <= static_cast<int>(cy) + 13; ++y)
                for (int x = static_cast<int>(cx) - 13; x <= static_cast<int>(cx) + 13; ++x) {
                    const float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                    if (d < 8.5f || d > 11.5f || x < 0 || y < 0 || x >= static_cast<int>(width) || y >= static_cast<int>(height)) continue;
                    const unsigned char* px = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
                    if (px[1] >= 180 && px[0] <= 110 && px[2] <= 110) ++n;
                }
            return n;
        };
        const auto menu_frames = ui::StatsForTest().frames;
        tag::Configure(true, true, tag::Settings{});
        tag::Frame frame{};
        frame.visible = true; frame.ndc[0] = 0.25f; frame.ndc[1] = 0.1f; frame.rect[2] = frame.rect[3] = 1.0f; frame.alpha = 1.0f;
        strcpy_s(frame.name, "GuestTag"); frame.seq = 1; frame.tick_ms = GetTickCount64();
        tag::Publish(frame);
        Check(shot(), "closed-menu frame with a fresh tag renders and reads back");
        auto stats = tag::GetRenderStats();
        const float expect_x = width * 0.625f, expect_y = height * 0.45f;
        Check(stats.drawn == 1 && std::fabs(stats.last_px[0] - expect_x) < 1.0f && std::fabs(stats.last_px[1] - expect_y) < 1.0f &&
              stats.bb[0] == width && stats.display[0] == width && stats.display[1] == height, "tag anchor at the projected backbuffer pixel");
        Check(stats.label_box[3] < expect_y && stats.label_box[0] < expect_x && stats.label_box[2] > expect_x, "label centred above the anchor");
        const unsigned white = lit(stats.label_box[0], stats.label_box[1], stats.label_box[2], stats.label_box[3], false);
        Check(white >= 30, "the label's white text is in the backbuffer");
        Check(ring_pixels(width * 0.5f, height * 0.5f) == 0 && !stats.crosshair, "no friendly ring without hover");
        Check(ui::StatsForTest().frames == menu_frames, "the tag layer does not count as a menu frame");
        frame.crosshair = frame.hover = true; frame.seq = 2; frame.tick_ms = GetTickCount64();
        tag::Publish(frame);
        Check(shot(), "frame with legacy crosshair intent");
        stats = tag::GetRenderStats();
        Check(!stats.crosshair && stats.crosshair_frames == 0, "legacy intent does not record an added crosshair");
        Check(ring_pixels(width * 0.5f, height * 0.5f) == 0, "hover does not add a green ring over the native reticle");
        const float box[4] = {stats.label_box[0], stats.label_box[1], stats.label_box[2], stats.label_box[3]};
        frame.tick_ms = GetTickCount64() - 1000; frame.seq = 3;
        tag::Publish(frame);
        Check(shot(), "stale frame presents");
        stats = tag::GetRenderStats();
        Check(stats.stale_frames >= 1 && lit(box[0], box[1], box[2], box[3], false) == 0 && ring_pixels(width * 0.5f, height * 0.5f) == 0,
              "a stale frame (paused game thread) draws nothing");
        frame.visible = frame.crosshair = false; frame.tick_ms = GetTickCount64(); frame.seq = 4;
        tag::Publish(frame);
        Check(shot() && tag::GetRenderStats().hidden_frames >= 1 && lit(box[0], box[1], box[2], box[3], false) == 0, "a hidden frame draws nothing");
        frame.visible = true; frame.tick_ms = GetTickCount64(); tag::Publish(frame);
        tag::Shutdown();
        Check(shot() && lit(box[0], box[1], box[2], box[3], false) == 0, "window shutdown hides the tag at once");
        tag::Configure(false, true, tag::Settings{});
        // The world-anchored rescue prompt: under a fresh name tag, else lower
        // center. MGS_HUD_DUMP_DIR saves each state as a BMP for review.
        {
            namespace hud = mgscoop::coop_hud;
            char dumpDir[MAX_PATH]{};
            const DWORD dumpLength = GetEnvironmentVariableA("MGS_HUD_DUMP_DIR", dumpDir, ARRAYSIZE(dumpDir));
            const auto dump = [&](const char* name) {
                if (!dumpLength || dumpLength >= ARRAYSIZE(dumpDir) || !rgba) return;
                char path[MAX_PATH]{};
                if (std::snprintf(path, sizeof(path), "%s\\%s.bmp", dumpDir, name) <= 0) return;
                FILE* out = nullptr;
                if (fopen_s(&out, path, "wb") || !out) return;
                const unsigned row = width * 3, pad = (4 - row % 4) % 4, image = (row + pad) * height;
                unsigned char header[54]{'B', 'M'};
                const unsigned fileSize = 54 + image;
                std::memcpy(header + 2, &fileSize, 4); header[10] = 54; header[14] = 40;
                std::memcpy(header + 18, &width, 4); std::memcpy(header + 22, &height, 4);
                header[26] = 1; header[28] = 24; std::memcpy(header + 34, &image, 4);
                std::fwrite(header, 1, 54, out);
                const unsigned char zero[3]{};
                for (unsigned y = height; y-- > 0;) {
                    for (unsigned x = 0; x < width; ++x) {
                        const unsigned char* px = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
                        const unsigned char bgr[3] = {px[2], px[1], px[0]};
                        std::fwrite(bgr, 1, 3, out);
                    }
                    std::fwrite(zero, 1, pad, out);
                }
                std::fclose(out);
            };
            hud::Configure(hud::Mode::compact, true);
            tag::Configure(true, true, tag::Settings{});
            tag::Frame anchor{};
            anchor.visible = true; anchor.ndc[0] = 0.0f; anchor.ndc[1] = 0.2f; anchor.rect[2] = anchor.rect[3] = 1.0f;
            anchor.alpha = 1.0f; anchor.dist_m = 12.4f; strcpy_s(anchor.name, "Guest");
            const auto publish = [&](bool down, bool close, bool range, std::uint32_t progress, std::uint8_t cause, bool self) {
                hud::DownedView view{};
                view.valid = true; view.mission = 30010; view.epoch = 7; view.publish_ms = GetTickCount64();
                view.localDown = self; view.localCause = self ? cause : 0;
                view.teammateDown = down && !self; view.teammateCause = down && !self ? cause : 0;
                view.closeEnough = close; view.inRange = range; view.progressMs = progress;
                hud::PublishDowned(view);
                anchor.seq++; anchor.tick_ms = GetTickCount64(); tag::Publish(anchor);
            };
            const float ax = width * 0.5f, ay = height * 0.4f;
            const float bx = width * 0.56f, by = height * 0.58f;
            publish(true, false, false, 0, 2, false);
            Check(shot() && lit(ax - 120, ay, ax + 120, ay + 40, false) >= 20, "away sleeper draws a status pill under the tag");
            Check(lit(bx, by - 30, bx + 230, by + 30, false) == 0, "an away teammate offers no Action button");
            dump("rescue_away");
            publish(true, true, true, 0, 2, false);
            Check(shot() && lit(bx, by - 30, bx + 230, by + 30, false) >= 40, "ready sleeper draws the Action button beside the player");
            dump("rescue_ready");
            SendMessageW(hwnd, WM_KEYDOWN, 'Q', 0);
            publish(true, true, true, 0, 2, false);
            Check(shot() && lit(bx, by - 30, bx + 230, by + 30, false) >= 40, "keyboard input switches to the E key cap");
            dump("rescue_ready_keyboard");
            publish(true, true, true, 1500, 1, false);
            Check(shot() && lit(bx, by - 30, bx + 230, by + 30, false) >= 60, "held revive draws the progress ring");
            dump("rescue_holding");
            anchor.visible = false;
            publish(true, true, true, 0, 1, false);
            Check(shot() && lit(bx, by - 30, bx + 230, by + 30, false) >= 40, "an off-screen teammate keeps the same button spot");
            dump("rescue_offscreen");
            anchor.visible = true;
            publish(true, false, false, 0, 2, true);
            Check(shot() && lit(ax - 220, height * 0.30f, ax + 220, height * 0.30f + 70, false) >= 40,
                  "the sleeping player sees a compact status above the stock icons");
            dump("rescue_self_asleep");
            // Over a coloured world the sleeper's view turns dark greyscale with
            // darker edges; the status drawn after it keeps its colour.
            const float world[4] = {0.2f, 0.7f, 0.3f, 1.0f};
            const auto shotOn = [&](const float* color) {
                ctx->ClearRenderTargetView(bb_rtv, color);
                ui::CaptureNextFrameForTest();
                return SUCCEEDED(chain->Present(0, 0)) && ui::CapturedFrameForTest(rgba, width, height) && width && height;
            };
            const auto pixel = [&](float x, float y) {
                return rgba + (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4;
            };
            const auto grey = [](const unsigned char* px) {
                const int lo = (std::min)(px[0], (std::min)(px[1], px[2])), hi = (std::max)(px[0], (std::max)(px[1], px[2]));
                return hi - lo <= 6;
            };
            Check(shotOn(world), "sleeping frame over a coloured world");
            const unsigned char* corner = pixel(8, 8);
            const unsigned char* low = pixel(width * 0.5f, height * 0.9f);
            Check(grey(corner) && grey(low), "the sleeper's world is greyscale");
            Check(low[1] < 100 && corner[1] < low[1], "the sleeper's world is dark, darker at the edges");
            Check(lit(ax - 220, height * 0.30f, ax + 220, height * 0.30f + 70, false) >= 40, "the sleeper's status stays bright over it");
            dump("rescue_self_asleep_world");
            publish(true, false, false, 0, 2, false);
            Check(shotOn(world) && pixel(8, 8)[1] > pixel(8, 8)[0] + 60, "a healthy player's world keeps its colour");
            hud::Configure(hud::Mode::off, true);
            publish(true, true, true, 0, 2, false);
            Check(shot() && lit(bx, by - 30, bx + 230, by + 30, false) >= 40,
                  "the one-PC lab default (status line off) still draws the rescue prompt");
            hud::Configure(hud::Mode::compact, true);
            publish(false, false, false, 0, 0, false);
            Check(shot() && lit(0, ay + 8, static_cast<float>(width), static_cast<float>(height), false) == 0,
                  "healthy pair draws no rescue prompt");
            hud::Configure(hud::Mode::off, false);
            tag::Configure(false, true, tag::Settings{});
        }
        Release(bb_rtv);
    }
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
