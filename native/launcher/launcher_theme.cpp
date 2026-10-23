// Diamond Dogs Together: GUI look and custom widgets (see launcher_theme.hpp).
#include "launcher_theme.hpp"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <string>

namespace ddt::theme {
namespace {
Fonts g_fonts;
float g_scale = 1.0f;

ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImU32 WithAlpha(ImU32 c, float alpha) {
    const unsigned a = static_cast<unsigned>(((c >> 24) & 0xFF) * alpha);
    return (c & 0x00FFFFFFu) | (static_cast<ImU32>(a > 255 ? 255 : a) << 24);
}

std::string FontPath(const wchar_t* file) {
    wchar_t dir[MAX_PATH]{};
    const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring path = std::wstring(dir) + L"\\Fonts\\" + file;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string utf8(static_cast<std::size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
    return utf8;
}

ImFont* LoadFont(const wchar_t* file, const char* name) {
    ImGuiIO& io = ImGui::GetIO();
    const std::string path = FontPath(file);
    ImFontConfig cfg;
    strncpy_s(cfg.Name, sizeof(cfg.Name), name, _TRUNCATE);
    if (!path.empty()) {
        if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), kBody, &cfg)) return font;
    }
    return io.Fonts->AddFontDefaultVector(&cfg);
}

const char* VisibleEnd(const char* label) {
    const char* hash = std::strstr(label, "##");
    return hash ? hash : label + std::strlen(label);
}
}  // namespace

Fonts LoadFonts() {
    g_fonts.regular = LoadFont(L"segoeui.ttf", "Segoe UI");
    g_fonts.semibold = LoadFont(L"seguisb.ttf", "Segoe UI Semibold");
    g_fonts.mono = LoadFont(L"consola.ttf", "Consolas");
    ImGui::GetIO().FontDefault = g_fonts.regular;
    return g_fonts;
}

const Fonts& GetFonts() { return g_fonts; }
float Scale() { return g_scale; }

void Apply(float scale) {
    g_scale = scale > 0.25f ? scale : 1.0f;
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(0, 0);
    s.WindowRounding = 0.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildRounding = 10.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupRounding = 10.0f;
    s.PopupBorderSize = 1.0f;
    s.FramePadding = ImVec2(11, 8);
    s.FrameRounding = 7.0f;
    s.FrameBorderSize = 1.0f;
    s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 10.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 6.0f;
    s.HoverDelayNormal = 0.35f;
    s.ScaleAllSizes(g_scale);
    s.FontSizeBase = kBody;
    s.FontScaleDpi = g_scale;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = V(col::kText);
    c[ImGuiCol_TextDisabled] = V(col::kTextFaint);
    c[ImGuiCol_WindowBg] = V(col::kWindow);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = V(col::kCard);
    c[ImGuiCol_Border] = V(col::kBorder);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = V(col::kField);
    c[ImGuiCol_FrameBgHovered] = V(col::Rgb(0x14, 0x16, 0x19));
    c[ImGuiCol_FrameBgActive] = V(col::Rgb(0x16, 0x18, 0x1B));
    c[ImGuiCol_TitleBg] = V(col::kSidebar);
    c[ImGuiCol_TitleBgActive] = V(col::kSidebar);
    c[ImGuiCol_TitleBgCollapsed] = V(col::kSidebar);
    c[ImGuiCol_MenuBarBg] = V(col::kSidebar);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = V(col::kBorderStrong);
    c[ImGuiCol_ScrollbarGrabHovered] = V(col::kTextFaint);
    c[ImGuiCol_ScrollbarGrabActive] = V(col::kTextDim);
    c[ImGuiCol_CheckMark] = V(col::kOnAccent);
    c[ImGuiCol_SliderGrab] = V(col::kAccent);
    c[ImGuiCol_SliderGrabActive] = V(col::kAccentActive);
    c[ImGuiCol_Button] = V(col::kCard);
    c[ImGuiCol_ButtonHovered] = V(col::kCardHover);
    c[ImGuiCol_ButtonActive] = V(col::kBorder);
    c[ImGuiCol_Header] = V(col::kAccentSoft);
    c[ImGuiCol_HeaderHovered] = V(col::kCardHover);
    c[ImGuiCol_HeaderActive] = V(col::kAccentSoft);
    c[ImGuiCol_Separator] = V(col::kBorder);
    c[ImGuiCol_SeparatorHovered] = V(col::kBorderStrong);
    c[ImGuiCol_SeparatorActive] = V(col::kAccent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_InputTextCursor] = V(col::kAccent);
    c[ImGuiCol_TextSelectedBg] = V(col::Rgb(0xE8, 0x86, 0x2E, 0x60));
    c[ImGuiCol_TextLink] = V(col::kAccent);
    c[ImGuiCol_NavCursor] = V(col::kAccent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.62f);
}

// ---- Icons -----------------------------------------------------------------
void DrawIcon(ImDrawList* d, Icon icon, ImVec2 c, float size, ImU32 color) {
    const float s = size;
    const float t = std::fmax(1.25f, s * 0.11f);   // stroke
    auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
    switch (icon) {
    case Icon::setup: {   // a clipboard with a tick
        d->AddRect(P(-0.34f, -0.36f), P(0.34f, 0.44f), color, s * 0.08f, 0, t);
        d->AddRectFilled(P(-0.16f, -0.46f), P(0.16f, -0.30f), color, s * 0.04f);
        const ImVec2 tick[3] = {P(-0.17f, 0.04f), P(-0.04f, 0.17f), P(0.18f, -0.08f)};
        d->AddPolyline(tick, 3, color, 0, t);
        break;
    }
    case Icon::play:
        d->AddTriangleFilled(P(-0.26f, -0.38f), P(0.40f, 0.0f), P(-0.26f, 0.38f), color);
        break;
    case Icon::signal:
        for (int i = 0; i < 3; ++i) {
            const float x0 = -0.40f + i * 0.30f;
            const float top = 0.20f - i * 0.28f;
            d->AddRectFilled(P(x0, top), P(x0 + 0.20f, 0.40f), color, s * 0.04f);
        }
        break;
    case Icon::tools: {   // a toolbox
        d->AddRect(P(-0.18f, -0.40f), P(0.18f, -0.18f), color, s * 0.05f, 0, t);
        d->AddRectFilled(P(-0.46f, -0.20f), P(0.46f, 0.40f), color, s * 0.08f);
        d->AddLine(P(-0.46f, 0.04f), P(0.46f, 0.04f), col::kSidebar, t * 0.9f);
        break;
    }
    case Icon::sliders:
        for (int i = 0; i < 3; ++i) {
            const float y = -0.30f + i * 0.30f;
            const float knob = i == 1 ? 0.18f : (i == 0 ? -0.16f : -0.02f);
            d->AddLine(P(-0.42f, y), P(0.42f, y), color, t);
            d->AddCircleFilled(P(knob, y), s * 0.12f, color);
        }
        break;
    case Icon::check: {
        const ImVec2 pts[3] = {P(-0.30f, 0.02f), P(-0.08f, 0.24f), P(0.32f, -0.20f)};
        d->AddPolyline(pts, 3, color, 0, std::fmax(1.6f, s * 0.14f));
        break;
    }
    case Icon::cross:
        d->AddLine(P(-0.24f, -0.24f), P(0.24f, 0.24f), color, std::fmax(1.6f, s * 0.14f));
        d->AddLine(P(-0.24f, 0.24f), P(0.24f, -0.24f), color, std::fmax(1.6f, s * 0.14f));
        break;
    case Icon::copy:
        d->AddRect(P(-0.38f, -0.20f), P(0.18f, 0.44f), color, s * 0.08f, 0, t);
        d->AddRect(P(-0.18f, -0.42f), P(0.38f, 0.22f), color, s * 0.08f, 0, t);
        break;
    case Icon::paste:
        d->AddRect(P(-0.34f, -0.34f), P(0.34f, 0.44f), color, s * 0.08f, 0, t);
        d->AddRectFilled(P(-0.16f, -0.46f), P(0.16f, -0.26f), color, s * 0.04f);
        d->AddLine(P(-0.16f, 0.06f), P(0.16f, 0.06f), color, t);
        d->AddLine(P(-0.16f, 0.22f), P(0.10f, 0.22f), color, t);
        break;
    case Icon::folder: {
        const ImVec2 pts[6] = {P(-0.44f, -0.32f), P(-0.12f, -0.32f), P(0.0f, -0.20f),
                               P(0.44f, -0.20f), P(0.44f, 0.36f),  P(-0.44f, 0.36f)};
        d->AddPolyline(pts, 6, color, ImDrawFlags_Closed, t);
        d->AddLine(P(-0.44f, -0.08f), P(0.44f, -0.08f), color, t);
        break;
    }
    case Icon::warning: {
        const ImVec2 pts[3] = {P(0.0f, -0.42f), P(0.46f, 0.38f), P(-0.46f, 0.38f)};
        d->AddPolyline(pts, 3, color, ImDrawFlags_Closed, t);
        d->AddLine(P(0.0f, -0.12f), P(0.0f, 0.10f), color, t);
        d->AddCircleFilled(P(0.0f, 0.24f), t * 0.7f, color);
        break;
    }
    case Icon::person:
        d->AddCircleFilled(P(0.0f, -0.18f), s * 0.20f, color);
        d->PathArcTo(P(0.0f, 0.44f), s * 0.38f, 3.14159265f, 2.0f * 3.14159265f);
        d->PathFillConvex(color);
        break;
    case Icon::share: {   // two arrows: out and in
        d->AddLine(P(-0.30f, -0.18f), P(0.34f, -0.18f), color, t);
        d->AddTriangleFilled(P(0.42f, -0.18f), P(0.22f, -0.32f), P(0.22f, -0.04f), color);
        d->AddLine(P(0.30f, 0.20f), P(-0.34f, 0.20f), color, t);
        d->AddTriangleFilled(P(-0.42f, 0.20f), P(-0.22f, 0.06f), P(-0.22f, 0.34f), color);
        break;
    }
    case Icon::logs:
        d->AddRect(P(-0.32f, -0.42f), P(0.32f, 0.42f), color, s * 0.08f, 0, t);
        for (int i = 0; i < 3; ++i) d->AddLine(P(-0.16f, -0.18f + i * 0.18f), P(0.16f, -0.18f + i * 0.18f), color, t);
        break;
    case Icon::trash:
        d->AddLine(P(-0.40f, -0.28f), P(0.40f, -0.28f), color, t);
        d->AddRect(P(-0.12f, -0.42f), P(0.12f, -0.28f), color, 0.0f, 0, t);
        d->AddRect(P(-0.30f, -0.20f), P(0.30f, 0.42f), color, s * 0.06f, 0, t);
        d->AddLine(P(-0.08f, -0.06f), P(-0.08f, 0.28f), color, t);
        d->AddLine(P(0.08f, -0.06f), P(0.08f, 0.28f), color, t);
        break;
    case Icon::info:
        d->AddCircle(c, s * 0.44f, color, 0, t);
        d->AddCircleFilled(P(0.0f, -0.20f), t * 0.75f, color);
        d->AddLine(P(0.0f, -0.04f), P(0.0f, 0.24f), color, t);
        break;
    case Icon::dot: d->AddCircleFilled(c, s * 0.25f, color); break;
    }
}

// ---- Widgets ---------------------------------------------------------------
bool Button(const char* label, ButtonKind kind, ImVec2 logical_size, bool enabled, Icon icon, bool with_icon) {
    const Font font(g_fonts.semibold, kBody);
    const char* end = VisibleEnd(label);
    const ImVec2 text = ImGui::CalcTextSize(label, end);
    const float icon_box = with_icon ? Px(16.0f) : 0.0f;
    const float icon_gap = with_icon && end != label ? Px(8.0f) : 0.0f;
    ImVec2 size(text.x + icon_box + icon_gap + Px(30.0f), Px(36.0f));
    if (logical_size.x > 0) size.x = Px(logical_size.x);
    if (logical_size.x < 0) size.x = ImGui::GetContentRegionAvail().x;
    if (logical_size.y > 0) size.y = Px(logical_size.y);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton(label, size) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    if (!enabled) ImGui::EndDisabled();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImU32 bg = 0, fg = col::kText, border = 0;
    switch (kind) {
    case ButtonKind::primary:
        bg = held ? col::kAccentActive : hovered ? col::kAccentHover : col::kAccent;
        fg = col::kOnAccent;
        break;
    case ButtonKind::secondary:
        bg = held ? col::kBorder : hovered ? col::kCardHover : col::kCard;
        border = hovered ? col::kBorderStrong : col::kBorder;
        break;
    case ButtonKind::danger:
        bg = held ? col::kBadActive : hovered ? col::kBadHover : col::kBad;
        fg = col::Rgb(0x1A, 0x0E, 0x0D);
        break;
    case ButtonKind::quiet:
        bg = held ? col::kBorder : hovered ? col::kCardHover : 0;
        fg = hovered ? col::kText : col::kTextDim;
        break;
    }
    if (!enabled) {
        bg = kind == ButtonKind::quiet ? 0 : col::Rgb(0x22, 0x25, 0x29);
        fg = col::kTextFaint;
        border = kind == ButtonKind::secondary ? col::kBorder : 0;
    }
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + size.x, p.y + size.y);
    const float rounding = Px(7.0f);
    if (bg) d->AddRectFilled(p, q, bg, rounding);
    if (border) d->AddRect(p, q, border, rounding, 0, std::fmax(1.0f, Px(1.0f)));
    const float content = icon_box + icon_gap + text.x;
    float x = p.x + (size.x - content) * 0.5f;
    const float cy = p.y + size.y * 0.5f;
    if (with_icon) {
        DrawIcon(d, icon, ImVec2(x + icon_box * 0.5f, cy), icon_box, fg);
        x += icon_box + icon_gap;
    }
    d->AddText(ImVec2(std::floor(x), std::floor(cy - text.y * 0.5f)), fg, label, end);
    return clicked;
}

void Text(const char* text, ImU32 color, float size, ImFont* font, float wrap_width) {
    const Font f(font ? font : g_fonts.regular, size);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    if (wrap_width < 0.0f) {
        ImGui::TextUnformatted(text);
    } else {
        const float wrap = wrap_width > 0.0f ? ImGui::GetCursorPosX() + wrap_width : 0.0f;
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
    }
    ImGui::PopStyleColor();
}

void SectionLabel(const char* text) { Text(text, col::kTextFaint, kLabel, g_fonts.semibold, -1.0f); }

bool Segmented(const char* id, const char* const* labels, int count, int* index, float logical_width, bool enabled,
               bool accent) {
    ImGui::PushID(id);
    const Font font(g_fonts.semibold, kBody);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float width = Px(logical_width);
    const float height = Px(38.0f);
    const float pad = Px(3.0f);
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col::kField, Px(9.0f));
    d->AddRect(p, ImVec2(p.x + width, p.y + height), col::kBorder, Px(9.0f), 0, std::fmax(1.0f, Px(1.0f)));
    const float seg = (width - 2 * pad) / static_cast<float>(count);
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const ImVec2 a(p.x + pad + seg * i, p.y + pad);
        const ImVec2 b(a.x + seg, p.y + height - pad);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (!enabled) ImGui::BeginDisabled();
        const bool clicked = ImGui::InvisibleButton("##seg", ImVec2(seg, b.y - a.y));
        const bool hovered = enabled && ImGui::IsItemHovered();
        if (!enabled) ImGui::EndDisabled();
        ImGui::PopID();
        const bool selected = *index == i;
        if (clicked && enabled) {   // also on the selected segment (it can select a muted toggle's option)
            *index = i;
            changed = true;
        }
        if (hovered && !selected) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImU32 fg = col::kTextDim;
        if (selected) {
            const bool strong = enabled && accent;
            d->AddRectFilled(a, b, strong ? (hovered ? col::kAccentHover : col::kAccent) : col::kBorderStrong, Px(7.0f));
            fg = strong ? col::kOnAccent : (enabled ? col::kText : col::kTextDim);
        } else if (hovered) {
            d->AddRectFilled(a, b, col::kCardHover, Px(7.0f));
            fg = col::kText;
        }
        if (!enabled && !selected) fg = col::kTextFaint;
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        d->AddText(ImVec2(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f)), fg, labels[i]);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopID();
    return changed;
}

bool RadioRow(const char* id, bool selected, const char* title, const char* description, float width, bool enabled,
              float extra_logical_height, ImVec2* extra_pos) {
    const float pad_x = Px(14.0f);
    const float pad_y = Px(10.0f);
    const float dot_area = Px(30.0f);
    const float text_w = width - pad_x * 2 - dot_area;
    float title_h, desc_h;
    {
        const Font f(g_fonts.semibold, kBody);
        title_h = ImGui::CalcTextSize(title).y;
    }
    {
        const Font f(g_fonts.regular, kSmall);
        desc_h = description && *description ? ImGui::CalcTextSize(description, nullptr, false, text_w).y : 0.0f;
    }
    const float gap = desc_h > 0 ? Px(2.0f) : 0.0f;
    const float extra = extra_logical_height > 0.0f ? Px(extra_logical_height) + Px(6.0f) : 0.0f;
    const float height = pad_y * 2 + title_h + gap + desc_h + extra;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height)) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    if (!enabled) ImGui::EndDisabled();
    if (hovered && !selected) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + width, p.y + height);
    const ImU32 bg = selected ? col::kAccentSoft : hovered ? col::kCardHover : col::kCard;
    const ImU32 border = selected ? WithAlpha(col::kAccent, enabled ? 0.85f : 0.4f) : col::kBorder;
    d->AddRectFilled(p, q, bg, Px(9.0f));
    d->AddRect(p, q, border, Px(9.0f), 0, std::fmax(1.0f, Px(selected ? 1.5f : 1.0f)));
    const ImVec2 dot(p.x + pad_x + Px(9.0f), p.y + pad_y + title_h * 0.5f);
    d->AddCircle(dot, Px(8.0f), selected ? col::kAccent : col::kBorderStrong, 0, std::fmax(1.2f, Px(1.6f)));
    if (selected) d->AddCircleFilled(dot, Px(4.2f), col::kAccent);
    const float tx = p.x + pad_x + dot_area;
    if (extra_pos) *extra_pos = ImVec2(tx, p.y + pad_y + title_h + gap + desc_h + Px(6.0f));
    const ImU32 title_col = enabled ? col::kText : col::kTextFaint;
    {
        const Font f(g_fonts.semibold, kBody);
        d->AddText(g_fonts.semibold, ImGui::GetFontSize(), ImVec2(tx, p.y + pad_y), title_col, title);
    }
    if (desc_h > 0) {
        const Font f(g_fonts.regular, kSmall);
        d->AddText(g_fonts.regular, ImGui::GetFontSize(), ImVec2(tx, p.y + pad_y + title_h + gap),
                   enabled ? col::kTextDim : col::kTextFaint, description, nullptr, text_w);
    }
    return clicked;
}

bool Checkbox(const char* label, bool* value, bool enabled) {
    const Font font(g_fonts.regular, kBody);
    const float box = Px(18.0f);
    const ImVec2 text = ImGui::CalcTextSize(label, VisibleEnd(label));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = std::fmax(box, text.y);
    if (!enabled) ImGui::BeginDisabled();
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(box + Px(10.0f) + text.x, h)) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    if (!enabled) ImGui::EndDisabled();
    if (clicked) *value = !*value;
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y + (h - box) * 0.5f);
    const ImVec2 b(a.x + box, a.y + box);
    if (*value) {
        d->AddRectFilled(a, b, enabled ? (hovered ? col::kAccentHover : col::kAccent) : col::kBorderStrong, Px(5.0f));
        DrawIcon(d, Icon::check, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), box * 0.9f, col::kOnAccent);
    } else {
        d->AddRectFilled(a, b, col::kField, Px(5.0f));
        d->AddRect(a, b, hovered ? col::kTextFaint : col::kBorderStrong, Px(5.0f), 0, std::fmax(1.0f, Px(1.4f)));
    }
    d->AddText(ImVec2(b.x + Px(10.0f), p.y + (h - text.y) * 0.5f), enabled ? col::kText : col::kTextFaint, label,
               VisibleEnd(label));
    return clicked;
}

void ProgressBar(float fraction, float logical_width, float logical_height, ImU32 fill) {
    if (!(fraction > 0.0f)) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = logical_width > 0 ? Px(logical_width) : ImGui::GetContentRegionAvail().x;
    const float h = Px(logical_height);
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col::kField, h * 0.5f);
    if (fraction > 0.0f) {
        const float fw = std::fmax(h, w * fraction);
        d->AddRectFilled(p, ImVec2(p.x + fw, p.y + h), fill, h * 0.5f);
    }
    ImGui::Dummy(ImVec2(w, h));
}

void Spinner(ImVec2 center, float logical_radius, ImU32 color, double time) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float r = Px(logical_radius);
    const int step = static_cast<int>(time * 10.0) & 7;
    for (int i = 0; i < 8; ++i) {
        const float angle = 3.14159265f * 2.0f * static_cast<float>(i) / 8.0f - 3.14159265f * 0.5f;
        const int age = (step - i + 8) & 7;
        const float alpha = 1.0f - static_cast<float>(age) / 8.0f;
        d->AddCircleFilled(ImVec2(center.x + std::cos(angle) * r, center.y + std::sin(angle) * r), r * 0.22f,
                           WithAlpha(color, 0.15f + 0.85f * alpha));
    }
}

void StatusDot(ImVec2 center, float logical_radius, ImU32 color) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddCircleFilled(center, Px(logical_radius) * 2.0f, WithAlpha(color, 0.18f));
    d->AddCircleFilled(center, Px(logical_radius), color);
}

void BeginCard(const char* id, float logical_height, ImU32 background, ImU32 border) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Px(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(18.0f, 16.0f));
    ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (logical_height <= 0.0f) flags |= ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(id, ImVec2(0.0f, logical_height > 0.0f ? Px(logical_height) : 0.0f), flags,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Px(10.0f, 8.0f));
}

void EndCard() {
    ImGui::PopStyleVar(1);
    ImGui::EndChild();
}

}  // namespace ddt::theme
