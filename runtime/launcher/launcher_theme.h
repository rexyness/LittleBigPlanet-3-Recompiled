// LBP3Launcher's look (launcher_theme.cpp): warm cardboard and stitches, drawn with ImGui shapes;
// Windows' own fonts (Segoe UI, Bahnschrift, Segoe UI Black; nothing downloaded or bundled); the
// game's art from the player's dump panning slowly behind everything; animated cards, PLAY button,
// status pill, page transitions and toasts. Every size goes through S() for the display's scale.
#pragma once

#include <imgui.h>

#include <string>

namespace launcher::ui {

struct App;
struct Texture;

// Palette
extern const ImVec4 kBg, kPanel, kCard, kCardHover, kSide, kStitch, kCardboard, kOrange, kText, kDim, kGood, kWarn, kBad, kInfo,
    kPlay, kPlayHover;
ImU32 Col(const ImVec4& c, float alpha_scale = 1.f);

// Fonts (sizes are 1x; ImGui scales them with the display)
extern ImFont* g_font;     // Segoe UI
extern ImFont* g_bold;     // Segoe UI Semibold
extern ImFont* g_heading;  // Bahnschrift
extern ImFont* g_black;    // Segoe UI Black
extern float g_scale;
inline float S(float v) { return v * g_scale; }
void LoadFonts();
void ApplyStyle(float scale);

struct Rect {
    ImVec2 min, max;
};

// Animation: a value per id that eases towards a target (0..1) at `speed` per second.
float Ease(ImGuiID id, float target, float speed = 10.f);
float Smooth(float t);  // smoothstep 0..1

// Small pieces
void Dim(const char* fmt, ...);
void Help(const char* text);
bool Toggle(const char* label, bool* v, const char* tip);
bool InputString(const char* label, std::string& value, const char* hint = nullptr, float width = 0.f, int flags = 0);
void Heading(const char* text);       // a section title with a stitched rule
void PageTitle(const char* title, const char* subtitle);
void Stitches(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float dash, float gap, float thickness);  // a dashed line
void StitchedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 color, float inset, float rounding);
// A card: a rounded cardboard panel with a stitched border that lifts when hovered.
bool BeginCard(const char* id, ImVec2 size, bool hoverable = true);
void EndCard();
// A big button in the launcher's style.
bool BigButton(const char* label, ImVec2 size, const ImVec4& color, bool enabled = true);
// A pill: a rounded label with a dot ("Online as Kistopawa").
void Pill(const char* text, const ImVec4& dot, bool pulse);
// The PLAY button: green, breathing, stitched; label "PLAY" / "PLAYING" / "STARTING".
bool PlayButton(const char* label, ImVec2 size, bool enabled, bool busy);
// A spinner for waits.
void Spinner(float radius, const ImVec4& color);
// Icons for the sidebar and cards, drawn with shapes.
enum class Icon { Home, Community, Online, Friends, Game, Graphics, Controls, Dlc, Saves, Pad, News, Tools, Help, Heart, Play, Star, Clock, Level };
void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// The art: full-window background, slowly panning (with a little parallax from the mouse).
void ArtBackground(App& a);
// Toasts: bottom-right, sliding in, fading out after a few seconds.
void DrawToasts(App& a);
// Page transition: call before drawing the page; returns the alpha to push.
float PageAlpha(App& a);

}  // namespace launcher::ui
