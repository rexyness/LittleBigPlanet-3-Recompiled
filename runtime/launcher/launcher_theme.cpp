// LBP3Launcher's look (launcher_theme.h).
#include "launcher_theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

#include "launcher_app.h"

namespace launcher::ui {

// Warm charcoal, cardboard and cream stitches (LBP's crafted look), readable text.
const ImVec4 kBg = ImVec4(0.090f, 0.075f, 0.059f, 1.f);         // #17130F
const ImVec4 kPanel = ImVec4(0.129f, 0.106f, 0.086f, 0.92f);    // #211B16
const ImVec4 kCard = ImVec4(0.169f, 0.137f, 0.110f, 0.94f);     // #2B231C
const ImVec4 kCardHover = ImVec4(0.212f, 0.165f, 0.125f, 0.97f);
const ImVec4 kSide = ImVec4(0.071f, 0.059f, 0.047f, 0.93f);     // #120F0C
const ImVec4 kStitch = ImVec4(0.914f, 0.843f, 0.710f, 1.f);     // #E9D7B5
const ImVec4 kCardboard = ImVec4(0.851f, 0.639f, 0.373f, 1.f);  // #D9A35F
const ImVec4 kOrange = ImVec4(0.941f, 0.541f, 0.235f, 1.f);     // #F08A3C
const ImVec4 kText = ImVec4(0.961f, 0.933f, 0.894f, 1.f);       // #F5EEE4
const ImVec4 kDim = ImVec4(0.725f, 0.667f, 0.592f, 1.f);        // #B9AA97
const ImVec4 kGood = ImVec4(0.498f, 0.820f, 0.545f, 1.f);
const ImVec4 kWarn = ImVec4(1.f, 0.776f, 0.361f, 1.f);
const ImVec4 kBad = ImVec4(1.f, 0.478f, 0.420f, 1.f);
const ImVec4 kInfo = ImVec4(0.494f, 0.737f, 1.f, 1.f);
const ImVec4 kPlay = ImVec4(0.247f, 0.682f, 0.353f, 1.f);
const ImVec4 kPlayHover = ImVec4(0.298f, 0.769f, 0.416f, 1.f);

ImFont* g_font = nullptr;
ImFont* g_bold = nullptr;
ImFont* g_heading = nullptr;
ImFont* g_black = nullptr;
float g_scale = 1.f;

ImU32 Col(const ImVec4& c, float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a)); }

void LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    char windir[MAX_PATH] = "C:\\Windows";
    GetEnvironmentVariableA("WINDIR", windir, MAX_PATH);
    const std::string dir = std::string(windir) + "\\Fonts\\";
    std::error_code ec;
    auto load = [&](std::initializer_list<const char*> names) -> ImFont* {
        for (const char* n : names)
            if (fs::exists(dir + n, ec)) return io.Fonts->AddFontFromFileTTF((dir + n).c_str(), 0.f);
        return nullptr;
    };
    // Segoe UI Variable is Windows 11's; Windows 10 has Segoe UI.
    g_font = load({"SegUIVar.ttf", "segoeui.ttf"});
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    // Level titles and names from the server can be in any language: Japanese, Chinese and Korean
    // glyphs come from Windows' own fonts (loaded on demand by ImGui, so this costs nothing until used).
    for (const char* cjk : {"YuGothM.ttc", "msyh.ttc", "msjh.ttc", "malgun.ttf", "seguisym.ttf", "seguiemj.ttf"}) {
        if (!fs::exists(dir + cjk, ec)) continue;
        ImFontConfig cfg;
        cfg.MergeMode = true;
        io.Fonts->AddFontFromFileTTF((dir + cjk).c_str(), 0.f, &cfg);
    }
    g_bold = load({"seguisb.ttf", "segoeuib.ttf"});
    g_heading = load({"bahnschrift.ttf", "seguisb.ttf"});
    g_black = load({"seguibl.ttf", "segoeuib.ttf"});
    if (!g_bold) g_bold = g_font;
    if (!g_heading) g_heading = g_bold;
    if (!g_black) g_black = g_bold;
    io.FontDefault = g_font;
}

void ApplyStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 0.f;
    st.FrameRounding = 8.f;
    st.GrabRounding = 8.f;
    st.TabRounding = 8.f;
    st.PopupRounding = 14.f;
    st.ChildRounding = 14.f;
    st.ScrollbarRounding = 8.f;
    st.FramePadding = ImVec2(12, 7);
    st.ItemSpacing = ImVec2(12, 10);
    st.ItemInnerSpacing = ImVec2(8, 6);
    st.WindowPadding = ImVec2(20, 16);
    st.FrameBorderSize = 1.f;
    st.PopupBorderSize = 1.5f;
    st.ScrollbarSize = 12.f;
    st.GrabMinSize = 14.f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(kCard.x, kCard.y, kCard.z, 0.99f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.03f, 0.02f, 0.01f, 0.62f);
    c[ImGuiCol_Border] = ImVec4(kStitch.x, kStitch.y, kStitch.z, 0.22f);
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_FrameBg] = ImVec4(0.235f, 0.188f, 0.149f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.302f, 0.239f, 0.184f, 1.f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.369f, 0.286f, 0.212f, 1.f);
    c[ImGuiCol_CheckMark] = kCardboard;
    c[ImGuiCol_Button] = ImVec4(0.302f, 0.231f, 0.169f, 1.f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.420f, 0.318f, 0.212f, 1.f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.541f, 0.408f, 0.255f, 1.f);
    c[ImGuiCol_Header] = ImVec4(kCardboard.x, kCardboard.y, kCardboard.z, 0.28f);
    c[ImGuiCol_HeaderHovered] = ImVec4(kCardboard.x, kCardboard.y, kCardboard.z, 0.40f);
    c[ImGuiCol_HeaderActive] = ImVec4(kCardboard.x, kCardboard.y, kCardboard.z, 0.55f);
    c[ImGuiCol_SliderGrab] = kCardboard;
    c[ImGuiCol_SliderGrabActive] = kOrange;
    c[ImGuiCol_Separator] = ImVec4(kStitch.x, kStitch.y, kStitch.z, 0.25f);
    c[ImGuiCol_Tab] = ImVec4(0.235f, 0.188f, 0.149f, 1.f);
    c[ImGuiCol_TabHovered] = ImVec4(0.420f, 0.318f, 0.212f, 1.f);
    c[ImGuiCol_TabSelected] = ImVec4(0.541f, 0.408f, 0.255f, 1.f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.212f, 0.165f, 0.125f, 1.f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    c[ImGuiCol_TableBorderLight] = ImVec4(kStitch.x, kStitch.y, kStitch.z, 0.10f);
    c[ImGuiCol_NavCursor] = kOrange;
    c[ImGuiCol_PlotHistogram] = kCardboard;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0.1f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(kStitch.x, kStitch.y, kStitch.z, 0.22f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(kOrange.x, kOrange.y, kOrange.z, 0.35f);
    st.ScaleAllSizes(scale);
    st.FontSizeBase = 17.f;
    st.FontScaleDpi = scale;
}

float Smooth(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float Ease(ImGuiID id, float target, float speed) {
    static std::unordered_map<ImGuiID, float> values;
    float& v = values[id];
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    v += (target - v) * std::min(1.f, dt * speed);
    if (std::fabs(target - v) < 0.002f) v = target;
    return v;
}

void Dim(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void Help(const char* text) {
    ImGui::SameLine();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = S(8);
    const float h = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(r * 2, h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + r, p.y + h * 0.5f);
    dl->AddCircle(c, r, Col(hovered ? kCardboard : kDim, hovered ? 1.f : 0.8f), 0, S(1.5f));
    const char* q = "?";
    ImGui::PushFont(g_bold, 13.f);
    const ImVec2 ts = ImGui::CalcTextSize(q);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Col(hovered ? kCardboard : kDim), q);
    ImGui::PopFont();
    if (hovered) {
        ImGui::SetNextWindowSize(ImVec2(S(420), 0));
        ImGui::BeginTooltip();
        ImGui::TextWrapped("%s", text);
        ImGui::EndTooltip();
    }
}

bool Toggle(const char* label, bool* v, const char* tip) {
    // A sliding switch, then the label.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight() * 0.78f, w = h * 1.8f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    const float lineh = ImGui::GetFrameHeight();
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const bool pressed = ImGui::InvisibleButton("##toggle", ImVec2(w + S(10) + labelSize.x, lineh));
    if (pressed) *v = !*v;
    const bool hovered = ImGui::IsItemHovered();
    const float t = Ease(ImGui::GetItemID(), *v ? 1.f : 0.f, 14.f);
    const ImVec2 a(p.x, p.y + (lineh - h) * 0.5f);
    const ImVec4 off(0.302f, 0.239f, 0.184f, 1.f);
    const ImVec4 on = kCardboard;
    const ImVec4 track(off.x + (on.x - off.x) * t, off.y + (on.y - off.y) * t, off.z + (on.z - off.z) * t, 1.f);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), Col(track), h * 0.5f);
    if (hovered) dl->AddRect(a, ImVec2(a.x + w, a.y + h), Col(kStitch, 0.5f), h * 0.5f, S(1));
    const float r = h * 0.5f - S(2.5f);
    dl->AddCircleFilled(ImVec2(a.x + h * 0.5f + (w - h) * t, a.y + h * 0.5f), r, Col(kText));
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
        dl->AddRect(ImVec2(a.x - S(3), a.y - S(3)), ImVec2(a.x + w + S(3), a.y + h + S(3)), Col(kOrange), h, S(2));
    ImGui::RenderText(ImVec2(p.x + w + S(10), p.y + (lineh - labelSize.y) * 0.5f), label);
    ImGui::PopID();
    if (tip) Help(tip);
    return pressed;
}

bool InputString(const char* label, std::string& value, const char* hint, float width, int flags) {
    char buf[1024];
    std::snprintf(buf, sizeof buf, "%s", value.c_str());
    if (width > 0.f) ImGui::SetNextItemWidth(S(width));
    const bool changed =
        hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof buf, flags) : ImGui::InputText(label, buf, sizeof buf, flags);
    if (changed) value = buf;
    return changed;
}

void Stitches(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, float dash, float gap, float thickness) {
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.f) return;
    const float ux = dx / len, uy = dy / len;
    for (float t = 0; t < len; t += dash + gap) {
        const float e = std::min(len, t + dash);
        dl->AddLine(ImVec2(a.x + ux * t, a.y + uy * t), ImVec2(a.x + ux * e, a.y + uy * e), color, thickness);
    }
}

void StitchedRect(ImDrawList* dl, ImVec2 mn, ImVec2 mx, ImU32 color, float inset, float rounding) {
    const float d = S(7), g = S(5), t = S(1.4f);
    const float x0 = mn.x + inset, y0 = mn.y + inset, x1 = mx.x - inset, y1 = mx.y - inset, r = std::max(0.f, rounding - inset);
    Stitches(dl, ImVec2(x0 + r, y0), ImVec2(x1 - r, y0), color, d, g, t);
    Stitches(dl, ImVec2(x0 + r, y1), ImVec2(x1 - r, y1), color, d, g, t);
    Stitches(dl, ImVec2(x0, y0 + r), ImVec2(x0, y1 - r), color, d, g, t);
    Stitches(dl, ImVec2(x1, y0 + r), ImVec2(x1, y1 - r), color, d, g, t);
}

void Heading(const char* text) {
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::PushFont(g_heading, 21.f);
    ImGui::TextColored(kCardboard, "%s", text);
    ImGui::PopFont();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    Stitches(ImGui::GetWindowDrawList(), ImVec2(p.x, p.y - S(3)), ImVec2(p.x + w, p.y - S(3)), Col(kStitch, 0.25f), S(6), S(5), S(1.2f));
    ImGui::Dummy(ImVec2(0, S(2)));
}

void PageTitle(const char* title, const char* subtitle) {
    ImGui::PushFont(g_heading, 32.f);
    ImGui::TextColored(kText, "%s", title);
    ImGui::PopFont();
    if (subtitle && *subtitle) Dim("%s", subtitle);
    ImGui::Dummy(ImVec2(0, S(4)));
}

namespace {
// Cards can nest; each depth has its own splitter, kept in place (an ImDrawListSplitter must not be
// copied or moved: its channels' buffers would be freed twice).
constexpr int kMaxCardDepth = 8;
struct CardFrame {
    ImGuiID id = 0;
    bool hoverable = false;
};
CardFrame g_cards[kMaxCardDepth];
ImDrawListSplitter g_splits[kMaxCardDepth];
int g_cardDepth = 0;
}  // namespace

bool BeginCard(const char* id, ImVec2 size, bool hoverable) {
    ImGui::PushID(id);
    const ImGuiID gid = ImGui::GetID("##card");
    ImGui::PopID();
    // The background is drawn after the contents (into channel 0, under them), so it has their size.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    IM_ASSERT(g_cardDepth < kMaxCardDepth);
    const int d = g_cardDepth++;
    g_cards[d] = {gid, hoverable};
    g_splits[d].Split(dl, 2);
    g_splits[d].SetCurrentChannel(dl, 1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(14)));
    const bool open = ImGui::BeginChild(id, size,
                                        ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened |
                                            (size.y <= 0 ? ImGuiChildFlags_AutoResizeY : 0),
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    return open;
}

void EndCard() {
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::EndChild();
    const int d = --g_cardDepth;
    const CardFrame& f = g_cards[d];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    g_splits[d].SetCurrentChannel(dl, 0);
    const float t = f.hoverable ? Ease(f.id, hovered ? 1.f : 0.f, 12.f) : 0.f;
    const float r = S(14), lift = S(3) * t;
    // shadow, body, edge, stitches
    dl->AddRectFilled(ImVec2(mn.x + S(2), mn.y + S(5) - lift * 0.3f), ImVec2(mx.x + S(2), mx.y + S(6)), Col(ImVec4(0, 0, 0, 0.35f + 0.15f * t)), r);
    const ImVec4 body(kCard.x + (kCardHover.x - kCard.x) * t, kCard.y + (kCardHover.y - kCard.y) * t, kCard.z + (kCardHover.z - kCard.z) * t,
                      kCard.w);
    dl->AddRectFilled(mn, mx, Col(body), r);
    dl->AddRect(mn, mx, Col(kCardboard, 0.18f + 0.25f * t), r, S(1.2f));
    StitchedRect(dl, mn, mx, Col(kStitch, 0.20f + 0.25f * t), S(6), r);
    g_splits[d].Merge(dl);
}

bool BigButton(const char* label, ImVec2 size, const ImVec4& color, bool enabled) {
    ImGui::PushStyleColor(ImGuiCol_Button, enabled ? color : ImVec4(0.3f, 0.27f, 0.24f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(1.f, color.x * 1.15f), std::min(1.f, color.y * 1.15f), std::min(1.f, color.z * 1.15f), 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x * 0.9f, color.y * 0.9f, color.z * 0.9f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(10));
    ImGui::PushFont(g_bold, 18.f);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label, size);
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return pressed;
}

void Pill(const char* text, const ImVec4& dot, bool pulse) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(g_bold, 15.f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float h = ts.y + S(10), w = ts.x + S(34);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Col(ImVec4(0.06f, 0.05f, 0.04f, 0.72f)), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), Col(dot, 0.55f), h * 0.5f, S(1.2f));
    const ImVec2 c(p.x + S(14), p.y + h * 0.5f);
    float a = 1.f;
    if (pulse) {
        const float t = static_cast<float>(ImGui::GetTime());
        a = 0.55f + 0.45f * std::sin(t * 4.f);
        dl->AddCircleFilled(c, S(5) + S(4) * (0.5f + 0.5f * std::sin(t * 4.f)), Col(dot, 0.25f));
    }
    dl->AddCircleFilled(c, S(4.5f), Col(dot, a));
    dl->AddText(ImVec2(p.x + S(25), p.y + S(5)), Col(kText), text);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(w, h));
}

bool PlayButton(const char* label, ImVec2 size, bool enabled, bool busy) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##play", size, ImGuiButtonFlags_None) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    const ImGuiID id = ImGui::GetItemID();
    const float hov = Ease(id, hovered ? 1.f : 0.f, 12.f);
    const float press = Ease(id + 1, held ? 1.f : 0.f, 25.f);
    const float t = static_cast<float>(ImGui::GetTime());
    const float scale = 1.f + 0.035f * hov - 0.04f * press;
    const ImVec2 c(p.x + size.x * 0.5f, p.y + size.y * 0.5f);
    const ImVec2 half(size.x * 0.5f * scale, size.y * 0.5f * scale);
    const ImVec2 mn(c.x - half.x, c.y - half.y), mx(c.x + half.x, c.y + half.y);
    const float r = S(18);
    ImVec4 base = enabled ? kPlay : ImVec4(0.32f, 0.29f, 0.26f, 1.f);
    if (busy) base = ImVec4(0.231f, 0.478f, 0.788f, 1.f);
    // the breathing glow
    if (enabled || busy) {
        const float breath = 0.5f + 0.5f * std::sin(t * (busy ? 3.f : 1.8f));
        for (int i = 4; i >= 1; --i) {
            const float g = S(4.f * i) * (0.6f + 0.4f * breath + 0.3f * hov);
            dl->AddRectFilled(ImVec2(mn.x - g, mn.y - g), ImVec2(mx.x + g, mx.y + g), Col(base, 0.05f + 0.025f * hov), r + g);
        }
    }
    dl->AddRectFilled(ImVec2(mn.x, mn.y + S(4)), ImVec2(mx.x, mx.y + S(6)), Col(ImVec4(0, 0, 0, 0.45f)), r);
    const ImVec4 top(std::min(1.f, base.x * (1.18f + 0.1f * hov)), std::min(1.f, base.y * (1.18f + 0.1f * hov)),
                     std::min(1.f, base.z * (1.18f + 0.1f * hov)), 1.f);
    dl->AddRectFilled(mn, mx, Col(base), r);
    dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + (mx.y - mn.y) * 0.55f), Col(top, 0.9f), r, ImDrawFlags_RoundCornersTop);
    // a moving sheen while hovered
    if (hov > 0.01f) {
        const float sx = mn.x + (mx.x - mn.x) * std::fmod(t * 0.6f, 1.6f) - S(40);
        dl->PushClipRect(mn, mx, true);
        dl->AddRectFilledMultiColor(ImVec2(sx, mn.y), ImVec2(sx + S(40), mx.y), Col(ImVec4(1, 1, 1, 0)), Col(ImVec4(1, 1, 1, 0.16f * hov)),
                                    Col(ImVec4(1, 1, 1, 0.16f * hov)), Col(ImVec4(1, 1, 1, 0)));
        dl->PopClipRect();
    }
    StitchedRect(dl, mn, mx, Col(kStitch, enabled ? 0.75f : 0.35f), S(7), r);
    // the triangle and the word
    ImGui::PushFont(g_black, 30.f * scale);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float tri = busy ? 0.f : S(16) * scale;
    const float total = ts.x + (tri > 0 ? tri + S(12) : 0);
    float x = c.x - total * 0.5f;
    if (tri > 0) {
        const float ty = c.y;
        dl->AddTriangleFilled(ImVec2(x, ty - tri * 0.62f), ImVec2(x, ty + tri * 0.62f), ImVec2(x + tri, ty), Col(kText));
        x += tri + S(12);
    }
    dl->AddText(ImVec2(x + S(1.5f), c.y - ts.y * 0.5f + S(2)), Col(ImVec4(0, 0, 0, 0.35f)), label);
    dl->AddText(ImVec2(x, c.y - ts.y * 0.5f), Col(kText), label);
    ImGui::PopFont();
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible)
        dl->AddRect(ImVec2(mn.x - S(5), mn.y - S(5)), ImVec2(mx.x + S(5), mx.y + S(5)), Col(kOrange), r + S(5), S(2.5f));
    return pressed;
}

void Spinner(float radius, const ImVec4& color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 c(p.x + radius, p.y + h * 0.5f);
    const float t = static_cast<float>(ImGui::GetTime());
    for (int i = 0; i < 8; ++i) {
        const float a = t * 6.f + i * (IM_PI * 2.f / 8.f);
        dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * radius * 0.75f, c.y + std::sin(a) * radius * 0.75f), radius * 0.18f,
                            Col(color, 0.2f + 0.8f * (i / 7.f)));
    }
    ImGui::Dummy(ImVec2(radius * 2, h));
}

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col) {
    const float t = std::max(1.f, s * 0.11f);
    const float h = s * 0.5f;
    switch (icon) {
    case Icon::Home:
        dl->AddTriangle(ImVec2(c.x - h, c.y - h * 0.05f), ImVec2(c.x, c.y - h), ImVec2(c.x + h, c.y - h * 0.05f), col, t);
        dl->AddRect(ImVec2(c.x - h * 0.65f, c.y - h * 0.05f), ImVec2(c.x + h * 0.65f, c.y + h * 0.85f), col, 0, t);
        break;
    case Icon::Community:
        dl->AddCircle(c, h * 0.9f, col, 0, t);
        dl->AddEllipse(c, ImVec2(h * 0.4f, h * 0.9f), col, 0, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.9f, c.y), ImVec2(c.x + h * 0.9f, c.y), col, t);
        break;
    case Icon::Online:
        for (int i = 1; i <= 3; ++i) dl->PathArcTo(ImVec2(c.x, c.y + h * 0.7f), h * 0.45f * i, -IM_PI * 0.78f, -IM_PI * 0.22f), dl->PathStroke(col, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y + h * 0.7f), t * 1.3f, col);
        break;
    case Icon::Friends:
        dl->AddCircle(ImVec2(c.x - h * 0.35f, c.y - h * 0.3f), h * 0.32f, col, 0, t);
        dl->AddCircle(ImVec2(c.x + h * 0.4f, c.y - h * 0.2f), h * 0.26f, col, 0, t);
        dl->PathArcTo(ImVec2(c.x - h * 0.35f, c.y + h * 0.75f), h * 0.6f, IM_PI * 1.05f, IM_PI * 1.95f), dl->PathStroke(col, t);
        dl->PathArcTo(ImVec2(c.x + h * 0.4f, c.y + h * 0.72f), h * 0.45f, IM_PI * 1.1f, IM_PI * 1.9f), dl->PathStroke(col, t);
        break;
    case Icon::Game:
    case Icon::Pad:
        dl->AddRect(ImVec2(c.x - h, c.y - h * 0.45f), ImVec2(c.x + h, c.y + h * 0.55f), col, h * 0.45f, t);
        dl->AddLine(ImVec2(c.x - h * 0.6f, c.y), ImVec2(c.x - h * 0.2f, c.y), col, t);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y - h * 0.2f), ImVec2(c.x - h * 0.4f, c.y + h * 0.2f), col, t);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.45f, c.y - h * 0.08f), t * 1.1f, col);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.25f, c.y + h * 0.15f), t * 1.1f, col);
        break;
    case Icon::Graphics:
        dl->AddRect(ImVec2(c.x - h, c.y - h * 0.7f), ImVec2(c.x + h, c.y + h * 0.45f), col, s * 0.06f, t);
        dl->AddLine(ImVec2(c.x - h * 0.4f, c.y + h * 0.85f), ImVec2(c.x + h * 0.4f, c.y + h * 0.85f), col, t);
        dl->AddLine(ImVec2(c.x, c.y + h * 0.45f), ImVec2(c.x, c.y + h * 0.85f), col, t);
        break;
    case Icon::Controls:
        dl->AddRect(ImVec2(c.x - h, c.y - h * 0.55f), ImVec2(c.x + h, c.y + h * 0.55f), col, s * 0.08f, t);
        for (int i = 0; i < 4; ++i) dl->AddRectFilled(ImVec2(c.x - h * 0.7f + i * h * 0.38f, c.y - h * 0.25f), ImVec2(c.x - h * 0.5f + i * h * 0.38f, c.y - h * 0.05f), col);
        dl->AddLine(ImVec2(c.x - h * 0.45f, c.y + h * 0.25f), ImVec2(c.x + h * 0.45f, c.y + h * 0.25f), col, t);
        break;
    case Icon::Dlc:
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.4f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), col, 0, t);
        dl->AddLine(ImVec2(c.x - h * 0.9f, c.y - h * 0.4f), ImVec2(c.x + h * 0.9f, c.y - h * 0.4f), col, t);
        dl->AddLine(ImVec2(c.x, c.y - h * 0.4f), ImVec2(c.x, c.y + h * 0.8f), col, t);
        dl->AddCircle(ImVec2(c.x - h * 0.25f, c.y - h * 0.65f), h * 0.25f, col, 0, t);
        dl->AddCircle(ImVec2(c.x + h * 0.25f, c.y - h * 0.65f), h * 0.25f, col, 0, t);
        break;
    case Icon::Saves:
        dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h * 0.8f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), col, s * 0.06f, t);
        dl->AddRect(ImVec2(c.x - h * 0.45f, c.y - h * 0.8f), ImVec2(c.x + h * 0.45f, c.y - h * 0.25f), col, 0, t);
        dl->AddRect(ImVec2(c.x - h * 0.5f, c.y + h * 0.1f), ImVec2(c.x + h * 0.5f, c.y + h * 0.8f), col, 0, t);
        break;
    case Icon::News:
    case Icon::Star: {
        ImVec2 pts[10];
        for (int i = 0; i < 10; ++i) {
            const float a = -IM_PI * 0.5f + i * IM_PI / 5.f, rr = (i % 2) ? h * 0.42f : h * 0.95f;
            pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
        }
        if (icon == Icon::Star) dl->AddConvexPolyFilled(pts, 10, col);
        else dl->AddPolyline(pts, 10, col, t, ImDrawFlags_Closed);
        break;
    }
    case Icon::Tools:
        dl->AddLine(ImVec2(c.x - h * 0.7f, c.y + h * 0.7f), ImVec2(c.x + h * 0.25f, c.y - h * 0.25f), col, t * 1.6f);
        dl->AddCircle(ImVec2(c.x + h * 0.45f, c.y - h * 0.45f), h * 0.38f, col, 0, t);
        break;
    case Icon::Help:
        dl->AddCircle(c, h * 0.9f, col, 0, t);
        dl->PathArcTo(ImVec2(c.x, c.y - h * 0.25f), h * 0.3f, -IM_PI, IM_PI * 0.4f), dl->PathStroke(col, t);
        dl->AddLine(ImVec2(c.x, c.y + h * 0.05f), ImVec2(c.x, c.y + h * 0.25f), col, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y + h * 0.5f), t * 0.9f, col);
        break;
    case Icon::Heart: {
        dl->PathClear();
        dl->PathArcTo(ImVec2(c.x - h * 0.42f, c.y - h * 0.2f), h * 0.45f, IM_PI * 0.75f, IM_PI * 2.0f);
        dl->PathArcTo(ImVec2(c.x + h * 0.42f, c.y - h * 0.2f), h * 0.45f, IM_PI * 1.0f, IM_PI * 2.25f);
        dl->PathLineTo(ImVec2(c.x, c.y + h * 0.85f));
        dl->PathFillConcave(col);
        break;
    }
    case Icon::Play:
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.55f, c.y - h * 0.75f), ImVec2(c.x - h * 0.55f, c.y + h * 0.75f), ImVec2(c.x + h * 0.75f, c.y), col);
        break;
    case Icon::Clock:
        dl->AddCircle(c, h * 0.9f, col, 0, t);
        dl->AddLine(c, ImVec2(c.x, c.y - h * 0.55f), col, t);
        dl->AddLine(c, ImVec2(c.x + h * 0.4f, c.y + h * 0.2f), col, t);
        break;
    case Icon::Level:
        dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - h * 0.85f), ImVec2(c.x + h * 0.85f, c.y + h * 0.85f), col, s * 0.12f, t);
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.6f, c.y + h * 0.6f), ImVec2(c.x - h * 0.05f, c.y - h * 0.2f), ImVec2(c.x + h * 0.3f, c.y + h * 0.6f), col);
        dl->AddCircleFilled(ImVec2(c.x + h * 0.4f, c.y - h * 0.35f), h * 0.18f, col);
        break;
    }
}

namespace {
ImTextureRef Ref(const Texture& t) { return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t.tex))); }
}  // namespace

void ArtBackground(App& a) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 p0 = vp->Pos, p1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    dl->AddRectFilled(p0, p1, Col(kBg));
    if (!a.art.tex) {
        // no dump art: warm cardboard dots
        for (float y = p0.y; y < p1.y; y += S(28))
            for (float x = p0.x + (static_cast<int>((y - p0.y) / S(28)) % 2) * S(14); x < p1.x; x += S(28))
                dl->AddCircleFilled(ImVec2(x, y), S(1.5f), Col(kCardboard, 0.08f));
        return;
    }
    // Cover the window with pic1, zoomed a little, panning slowly (a 60 s loop) with a touch of
    // mouse parallax. In --shot the clock is fixed, so the pictures are the same every run.
    const float W = p1.x - p0.x, H = p1.y - p0.y;
    const float ar = W / H, tar = static_cast<float>(a.art.w) / static_cast<float>(a.art.h);
    float uw = 1.f, vh = 1.f;
    if (tar > ar) uw = ar / tar;
    else vh = tar / ar;
    const float zoom = 0.88f;  // show 88% of the covered area, leaving room to move
    uw *= zoom;
    vh *= zoom;
    const float t = static_cast<float>(a.time);
    float px = 0.5f + 0.5f * std::sin(t * 2.f * IM_PI / 60.f);
    float py = 0.5f + 0.5f * std::sin(t * 2.f * IM_PI / 85.f + 1.3f);
    if (!a.shot_mode) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        if (m.x > -1e5f) {
            px = std::clamp(px + ((m.x - p0.x) / W - 0.5f) * 0.12f, 0.f, 1.f);
            py = std::clamp(py + ((m.y - p0.y) / H - 0.5f) * 0.12f, 0.f, 1.f);
        }
    }
    const float u0 = (1.f - uw) * px, v0 = (1.f - vh) * py;
    dl->AddImage(Ref(a.art), p0, p1, ImVec2(u0, v0), ImVec2(u0 + uw, v0 + vh));
    // darken for reading: strong at the bottom and left, lighter behind the header
    dl->AddRectFilledMultiColor(p0, p1, Col(ImVec4(0.06f, 0.045f, 0.03f, 0.70f)), Col(ImVec4(0.06f, 0.045f, 0.03f, 0.55f)),
                                Col(ImVec4(0.06f, 0.045f, 0.03f, 0.88f)), Col(ImVec4(0.06f, 0.045f, 0.03f, 0.92f)));
    // a soft vignette
    const float v = S(120);
    dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, p0.y + v), Col(ImVec4(0, 0, 0, 0.35f)), Col(ImVec4(0, 0, 0, 0.35f)), Col(ImVec4(0, 0, 0, 0)),
                                Col(ImVec4(0, 0, 0, 0)));
}

void DrawToasts(App& a) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->Pos.y + vp->Size.y - S(24);
    const double now = a.time;
    a.toasts.erase(std::remove_if(a.toasts.begin(), a.toasts.end(), [&](const Toast& t) { return now - t.born > 5.0; }), a.toasts.end());
    ImGui::PushFont(g_bold, 16.f);
    for (auto it = a.toasts.rbegin(); it != a.toasts.rend(); ++it) {
        const float age = static_cast<float>(now - it->born);
        const float in = Smooth(age / 0.25f), out = 1.f - Smooth((age - 4.4f) / 0.6f);
        const float alpha = in * out;
        const float maxw = S(460);
        const ImVec2 ts = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, maxw - S(40));
        const float w = ts.x + S(44), h = ts.y + S(24);
        const float x = vp->Pos.x + vp->Size.x - S(24) - w + (1.f - in) * S(60);
        y -= h;
        dl->AddRectFilled(ImVec2(x + S(2), y + S(4)), ImVec2(x + w + S(2), y + h + S(4)), Col(ImVec4(0, 0, 0, 0.4f), alpha), S(12));
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), Col(kCard, alpha), S(12));
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + S(6), y + h), Col(it->color, alpha), S(12), ImDrawFlags_RoundCornersLeft);
        StitchedRect(dl, ImVec2(x, y), ImVec2(x + w, y + h), Col(kStitch, 0.35f * alpha), S(5), S(12));
        dl->AddText(nullptr, 0.f, ImVec2(x + S(22), y + S(12)), Col(kText, alpha), it->text.c_str(), nullptr, maxw - S(40));
        y -= S(10);
    }
    ImGui::PopFont();
}

float PageAlpha(App& a) {
    if (a.page != a.prev_page) {
        a.prev_page = a.page;
        a.page_since = a.time;
    }
    return Smooth(static_cast<float>((a.time - a.page_since) / 0.22));
}

}  // namespace launcher::ui
