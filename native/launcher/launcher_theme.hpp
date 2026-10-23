#pragma once
// Diamond Dogs Together: the GUI look (dark, MGSV-style orange accent) and the
// small set of custom widgets the pages are built from. Every size passed to
// these helpers is in logical (96-DPI) pixels; Px() applies the DPI scale.
#include "imgui.h"

namespace ddt::theme {

// ---- Colours ---------------------------------------------------------------
namespace col {
inline constexpr ImU32 Rgb(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return (static_cast<ImU32>(a) << 24) | (static_cast<ImU32>(b) << 16) | (static_cast<ImU32>(g) << 8) | r;
}
inline constexpr ImU32 kWindow = Rgb(0x17, 0x19, 0x1C);
inline constexpr ImU32 kSidebar = Rgb(0x10, 0x11, 0x13);
inline constexpr ImU32 kCard = Rgb(0x1F, 0x22, 0x26);
inline constexpr ImU32 kCardHover = Rgb(0x26, 0x2A, 0x2F);
inline constexpr ImU32 kField = Rgb(0x0F, 0x10, 0x12);
inline constexpr ImU32 kBorder = Rgb(0x2F, 0x33, 0x39);
inline constexpr ImU32 kBorderStrong = Rgb(0x43, 0x48, 0x4F);
inline constexpr ImU32 kText = Rgb(0xEC, 0xEA, 0xE6);
inline constexpr ImU32 kTextDim = Rgb(0xA2, 0xA7, 0xAD);
inline constexpr ImU32 kTextFaint = Rgb(0x6B, 0x71, 0x78);
inline constexpr ImU32 kAccent = Rgb(0xE8, 0x86, 0x2E);
inline constexpr ImU32 kAccentHover = Rgb(0xF3, 0x9C, 0x4B);
inline constexpr ImU32 kAccentActive = Rgb(0xCC, 0x70, 0x1E);
inline constexpr ImU32 kAccentSoft = Rgb(0xE8, 0x86, 0x2E, 0x26);
inline constexpr ImU32 kOnAccent = Rgb(0x16, 0x12, 0x0E);
inline constexpr ImU32 kGood = Rgb(0x5C, 0xC0, 0x7A);
inline constexpr ImU32 kGoodSoft = Rgb(0x5C, 0xC0, 0x7A, 0x22);
inline constexpr ImU32 kWarn = Rgb(0xE6, 0xB8, 0x45);
inline constexpr ImU32 kWarnSoft = Rgb(0xE6, 0xB8, 0x45, 0x22);
inline constexpr ImU32 kBad = Rgb(0xEB, 0x5D, 0x55);
inline constexpr ImU32 kBadSoft = Rgb(0xEB, 0x5D, 0x55, 0x24);
inline constexpr ImU32 kBadHover = Rgb(0xF2, 0x74, 0x6C);
inline constexpr ImU32 kBadActive = Rgb(0xC9, 0x46, 0x3F);
}  // namespace col

// ---- Fonts and scale -------------------------------------------------------
struct Fonts {
    ImFont* regular = nullptr;
    ImFont* semibold = nullptr;
    ImFont* mono = nullptr;
};
// Font sizes (logical px).
inline constexpr float kBody = 15.0f;
inline constexpr float kSmall = 13.0f;
inline constexpr float kLabel = 12.0f;
inline constexpr float kTitle = 25.0f;
inline constexpr float kHeading = 19.0f;
inline constexpr float kCode = 25.0f;

// Loads Segoe UI / Segoe UI Semibold / Consolas from the Windows font folder
// (each falls back to ImGui's built-in vector font). Call once, before the first frame.
Fonts LoadFonts();
const Fonts& GetFonts();
// Applies the style for a DPI scale (1.0 = 96 DPI). Safe to call again on a DPI change.
void Apply(float scale);
float Scale();
inline float Px(float logical) { return logical * Scale(); }
inline ImVec2 Px(float x, float y) { return ImVec2(x * Scale(), y * Scale()); }

// RAII font push: Font f(GetFonts().semibold, kHeading);
struct Font {
    Font(ImFont* font, float size) { ImGui::PushFont(font, size); }
    ~Font() { ImGui::PopFont(); }
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
};

// ---- Icons (drawn, no icon font) ---------------------------------------------
enum class Icon { setup, play, signal, tools, sliders, check, cross, copy, paste, folder, warning, person, share,
                  logs, trash, info, dot };
void DrawIcon(ImDrawList* draw, Icon icon, ImVec2 center, float size, ImU32 color);

// ---- Widgets ---------------------------------------------------------------
enum class ButtonKind { primary, secondary, danger, quiet };
// A button with an optional icon; disabled buttons draw faded and never return true.
bool Button(const char* label, ButtonKind kind, ImVec2 logical_size = ImVec2(0, 0), bool enabled = true,
            Icon icon = Icon::dot, bool with_icon = false);
// Wrapped text in a colour and font size (logical px).
void Text(const char* text, ImU32 color, float size = kBody, ImFont* font = nullptr, float wrap_width = 0.0f);
// Small upper-case label above a section.
void SectionLabel(const char* text);
// Segmented control (Host | Join). Returns true when a segment was clicked (*index is set to it).
// `accent` = false draws the selected segment muted (an inactive option's toggle).
bool Segmented(const char* id, const char* const* labels, int count, int* index, float logical_width, bool enabled,
               bool accent = true);
// A selectable row with a radio dot, title and description. Returns true when
// clicked. `extra_logical_height` reserves room under the text for another
// widget (its top-left is returned in *extra_pos); later items may overlap the row.
bool RadioRow(const char* id, bool selected, const char* title, const char* description, float width, bool enabled,
              float extra_logical_height = 0.0f, ImVec2* extra_pos = nullptr);
// A checkbox drawn in the theme (accent box with a tick). Returns true when toggled.
bool Checkbox(const char* label, bool* value, bool enabled);
// Thin rounded progress bar (fraction 0..1).
void ProgressBar(float fraction, float logical_width, float logical_height, ImU32 fill);
// Ticking spinner (8 dots, steps every 100 ms so it stays cheap to redraw).
void Spinner(ImVec2 center, float logical_radius, ImU32 color, double time);
// A filled round status dot with a soft halo.
void StatusDot(ImVec2 center, float logical_radius, ImU32 color);
// Cards: a rounded panel sized to its contents (height 0) or fixed.
void BeginCard(const char* id, float logical_height = 0.0f, ImU32 background = col::kCard, ImU32 border = col::kBorder);
void EndCard();

}  // namespace ddt::theme
