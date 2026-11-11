// The PS4 system dialogs over the game window (see ui/dialog_overlay.h), a layer of the
// overlay host (ui/overlay.cpp), which owns ImGui. State that any thread touches (requested
// and open dialogs, progress updates) is under g_mutex; everything else belongs to the
// presenter thread.
//
// The look follows the on-screen keyboard (ui/osk_overlay.cpp): a dark panel with a thin
// border, light text, buttons in the keyboard's key colours with the focused one in its
// accent blue and a white border, and a hint row with the Cross and Circle glyphs. Icons and
// glyphs are drawn as shapes. Behaviour follows hle/dialog_window.cpp's native windows.
#include "ui/dialog_overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "hle/pad_types.h"
#include "input/gamepad.h"
#include "ui/dialog_testing.h"
#include "ui/osk_model.h"
#include "ui/overlay.h"
#include "ui/overlay_layers.h"

namespace ui::dialog {
namespace {

namespace host = ui::overlay::detail;
using ui::osk::Repeater;
static_assert(sizeof(wchar_t) == sizeof(char16_t), "UTF-16 wide strings (Windows)");

// ---- Shared between threads (g_mutex) ----

struct Pending {
    uint64_t id = 0, stamp = 0;
    Request request;
    AnswerFn onAnswer;
};

std::mutex g_mutex;
std::vector<Pending> g_pending;                              // shown, not yet picked up
std::vector<uint64_t> g_open;                                // shown and not answered or closed
std::vector<std::pair<uint64_t, uint32_t>> g_progressQueue;  // for dialogs already picked up
std::atomic<int> g_openCount{0};
uint64_t g_nextId = 1;

// ---- Presenter thread ----

const ps4::PadData* g_testPad = nullptr;
uint64_t (*g_testClock)() = nullptr;
ImFont* g_font = nullptr;  // the host's fonts, refreshed each frame
ImFont* g_bold = nullptr;

uint64_t NowMs() { return g_testClock ? g_testClock() : SDL_GetTicks(); }

constexpr int kStickThreshold = 64;  // of 128: a clear push
constexpr int kStickCentre = 40;     // back inside this = released
constexpr uint64_t kDoubleClickMs = 400;

struct Live {
    uint64_t id = 0, stamp = 0;
    Request request;
    AnswerFn onAnswer;
    int focus = 1;          // messages and progress: the focused button
    int selected = -1;      // lists
    int firstRow = 0;       // lists: the first visible row
    uint32_t progress = 0;
    bool wasTop = true;     // false while a newer overlay has the input
    uint32_t ignore = 0, prev = 0;
    bool stickIgnored = false;
    Repeater up, down, left, right;
    bool enterHeld = false, escHeld = false;
    // Navigation keys (arrows, Tab, Home/End, Page Up/Down) held when the dialog came up, for
    // instance still auto-repeating from the game's menu: ignored until released, or one could
    // move the focus from No to Yes before the player sees the question.
    std::vector<SDL_Scancode> navHeld;
    std::vector<SDL_Texture*> icons;  // per list entry; nullptr = none. Freed in EndLive.
    ImVec2 lastMouse{-1.f, -1.f};
    bool mouseSeen = false;
    int lastClickRow = -1;
    uint64_t lastClickMs = 0;
};

std::vector<Live> g_live;  // oldest first; the last one takes the input

std::string Utf8(const std::wstring& w) {
    return ui::osk::Utf8(std::u16string_view(reinterpret_cast<const char16_t*>(w.data()), w.size()));
}

std::string Short(const std::wstring& w) {  // for the log
    std::string s = Utf8(w);
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    if (s.size() > 90) s = s.substr(0, 87) + "...";
    return s;
}

const char* KindName(Kind k) {
    switch (k) {
    case Kind::List: return "list";
    case Kind::Progress: return "progress";
    default: return "message";
    }
}

bool HasButton(const Request& r, int b) { return (b == 1 && !r.button1.empty()) || (b == 2 && !r.button2.empty()); }

std::string ButtonName(const Request& r, int b) {
    if (b == 1) return Utf8(r.button1);
    if (b == 2) return Utf8(r.button2);
    return b == kDismiss ? "(cancel)" : "(none)";
}

int InitialFocus(const Request& r) {
    if (HasButton(r, r.defaultButton)) return r.defaultButton;
    if (HasButton(r, 1)) return 1;
    if (HasButton(r, 2)) return 2;
    return r.defaultButton;
}

int VisibleRows(const Request& r) { return std::clamp(static_cast<int>(r.items.size()), 3, 6); }

bool ReadPad(ps4::PadData& pad) {
    if (g_testPad) {
        pad = *g_testPad;
        return true;
    }
    return input::Read(0, pad, false);
}

// Whatever is held now counts only after a release: when the dialog comes up, and when it is
// back on top after a newer overlay closed.
void Rearm(Live& l) {
    ps4::PadData pad{};
    const bool ok = ReadPad(pad);
    l.ignore = ok ? pad.buttons : 0;
    l.prev = 0;
    l.stickIgnored = ok && (std::abs(int(pad.leftStick.x) - 128) > kStickCentre ||
                            std::abs(int(pad.leftStick.y) - 128) > kStickCentre);
    for (Repeater* r : {&l.up, &l.down, &l.left, &l.right}) r->Reset();
    const bool* keys = SDL_GetKeyboardState(nullptr);
    l.enterHeld = keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER] || keys[SDL_SCANCODE_SPACE]);
    l.escHeld = keys && keys[SDL_SCANCODE_ESCAPE];
    l.navHeld.clear();
    for (SDL_Scancode c : {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_TAB,
                           SDL_SCANCODE_HOME, SDL_SCANCODE_END, SDL_SCANCODE_PAGEUP, SDL_SCANCODE_PAGEDOWN})
        if (keys && keys[c]) l.navHeld.push_back(c);
}

void EnsureVisible(Live& l) {
    const int n = static_cast<int>(l.request.items.size()), rows = VisibleRows(l.request);
    if (l.selected < l.firstRow) l.firstRow = l.selected;
    if (l.selected >= l.firstRow + rows) l.firstRow = l.selected - rows + 1;
    l.firstRow = std::clamp(l.firstRow, 0, std::max(0, n - rows));
}

SDL_Texture* LoadIcon(const std::wstring& path) {
    SDL_Renderer* r = host::Renderer();
    if (path.empty() || !r) return nullptr;
    const std::string p = Utf8(path);
    SDL_Surface* surface = SDL_LoadPNG(p.c_str());
    if (!surface) {
        std::fprintf(stderr, "[dlg] icon %s not loaded: %s\n", p.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, surface);
    SDL_DestroySurface(surface);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

void StartLive(Pending p) {
    host::EnsureImGui();
    Live l;
    l.id = p.id;
    l.stamp = p.stamp;
    l.request = std::move(p.request);
    l.onAnswer = std::move(p.onAnswer);
    l.focus = InitialFocus(l.request);
    const int n = static_cast<int>(l.request.items.size());
    l.selected = n ? std::clamp(l.request.focus, 0, n - 1) : -1;
    l.progress = std::min<uint32_t>(l.request.progress, 100);
    for (const Item& item : l.request.items) l.icons.push_back(LoadIcon(item.iconPath));
    Rearm(l);
    if (n) EnsureVisible(l);
    std::fprintf(stderr, "[dlg] #%llu up: %s \"%s\"\n", static_cast<unsigned long long>(l.id), KindName(l.request.kind),
                 Short(l.request.text).c_str());
    g_live.push_back(std::move(l));
}

void EndLive(size_t index) {
    if (index >= g_live.size()) return;
    for (SDL_Texture* t : g_live[index].icons)
        if (t) SDL_DestroyTexture(t);
    g_live.erase(g_live.begin() + static_cast<std::ptrdiff_t>(index));
    host::ReleaseMouse();
}

// Picks up Show, Close and SetProgress from other threads. Returns the newest dialog's stamp.
uint64_t Sync() {
    std::vector<Pending> take;
    std::vector<uint64_t> open;
    std::vector<std::pair<uint64_t, uint32_t>> progress;
    {
        std::lock_guard lock(g_mutex);
        take.swap(g_pending);
        open = g_open;
        progress.swap(g_progressQueue);
    }
    const auto isOpen = [&](uint64_t id) { return std::find(open.begin(), open.end(), id) != open.end(); };
    for (size_t i = g_live.size(); i-- > 0;)
        if (!isOpen(g_live[i].id)) EndLive(i);  // closed by the game or a script
    for (Pending& p : take)
        if (isOpen(p.id)) StartLive(std::move(p));
    for (const auto& [id, pct] : progress)
        for (Live& l : g_live)
            if (l.id == id) l.progress = std::min<uint32_t>(pct, 100);
    return g_live.empty() ? 0 : g_live.back().stamp;
}

// The player answered the top dialog: take it down, then answer (unless Close got there first).
void Answer(int button, int item) {
    if (g_live.empty()) return;
    Live& l = g_live.back();
    AnswerFn fn = std::move(l.onAnswer);
    const uint64_t id = l.id;
    const std::string name = ButtonName(l.request, button);
    bool current = false;
    {
        std::lock_guard lock(g_mutex);
        const auto it = std::find(g_open.begin(), g_open.end(), id);
        if (it != g_open.end()) {
            g_open.erase(it);
            current = true;
        }
        g_openCount = static_cast<int>(g_open.size());
    }
    EndLive(g_live.size() - 1);
    if (!current) return;
    if (button == kDismiss) {
        std::fprintf(stderr, "[dlg] #%llu answered: cancelled\n", static_cast<unsigned long long>(id));
    } else if (item >= 0) {
        std::fprintf(stderr, "[dlg] #%llu answered: button %d \"%s\", item %d\n", static_cast<unsigned long long>(id),
                     button, name.c_str(), item);
    } else {
        std::fprintf(stderr, "[dlg] #%llu answered: button %d \"%s\"\n", static_cast<unsigned long long>(id), button,
                     name.c_str());
    }
    if (fn) fn(ui::dialog::Answer{button, item});
}

// As the native window's Press: nothing for a button that isn't there, or for a list with
// nothing to choose. True once answered (the dialog is then gone: don't touch `l` again).
bool Press(Live& l, int button) {
    if (button == 0) return false;
    if (button != kDismiss && !HasButton(l.request, button)) return false;
    int item = -1;
    if (l.request.kind == Kind::List && button == 1) {
        if (l.selected < 0) return false;
        item = l.selected;
    }
    Answer(button, item);
    return true;
}

// The button Cross and Enter press: a list's is always the first (choose the selected entry).
int FocusedButton(const Live& l) { return l.request.kind == Kind::List ? 1 : l.focus; }

void MoveSelection(Live& l, int delta) {
    const int n = static_cast<int>(l.request.items.size());
    if (n == 0) return;
    l.selected = std::clamp(l.selected + delta, 0, n - 1);
    EnsureVisible(l);
}

void MoveFocus(Live& l, int to) {
    if (HasButton(l.request, to)) l.focus = to;
}

// False once the dialog has answered.
bool PollPad(Live& l) {
    ps4::PadData pad{};
    const bool ok = ReadPad(pad);
    const uint32_t raw = ok ? pad.buttons : 0;
    l.ignore &= raw;  // released buttons stop being ignored
    const uint32_t b = raw & ~l.ignore;
    const auto pressed = [&](uint32_t mask) { return (b & mask) && !(l.prev & mask); };

    bool sUp = false, sDown = false, sLeft = false, sRight = false;
    if (ok) {
        const int sx = int(pad.leftStick.x) - 128, sy = int(pad.leftStick.y) - 128;
        if (l.stickIgnored && std::abs(sx) <= kStickCentre && std::abs(sy) <= kStickCentre) l.stickIgnored = false;
        if (!l.stickIgnored && std::max(std::abs(sx), std::abs(sy)) > kStickThreshold) {
            if (std::abs(sy) >= std::abs(sx)) {
                (sy < 0 ? sUp : sDown) = true;
            } else {
                (sx < 0 ? sLeft : sRight) = true;
            }
        }
    }
    const uint64_t now = NowMs();
    const int fUp = l.up.Update((b & ps4::PAD_UP) || sUp, now);
    const int fDown = l.down.Update((b & ps4::PAD_DOWN) || sDown, now);
    const int fLeft = l.left.Update((b & ps4::PAD_LEFT) || sLeft, now);
    const int fRight = l.right.Update((b & ps4::PAD_RIGHT) || sRight, now);
    if (l.request.kind == Kind::List) {
        if (fUp) MoveSelection(l, -1);
        if (fDown) MoveSelection(l, 1);
    } else {
        if (fUp == 1 || fLeft == 1) MoveFocus(l, 1);
        if (fDown == 1 || fRight == 1) MoveFocus(l, 2);
    }
    if (pressed(ps4::PAD_CROSS | ps4::PAD_TOUCH_PAD)) {
        l.prev = b;
        if (Press(l, FocusedButton(l))) return false;
    } else if (pressed(ps4::PAD_CIRCLE)) {
        l.prev = b;
        if (Press(l, l.request.cancelButton)) return false;
    }
    l.prev = b;
    return true;
}

// ---- Layout ----

struct Box {
    ImVec2 min, max;
    bool Contains(ImVec2 p) const { return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y; }
    ImVec2 Size() const { return {max.x - min.x, max.y - min.y}; }
    ImVec2 Centre() const { return {(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f}; }
};

struct HintItem {
    bool present = false;
    std::string text;  // "Enter", "Back", or a list's action ("Save", "Load", "Delete")
    Box hit;           // the hover/click area
    ImVec2 glyph{};    // the glyph's centre
    ImVec2 label{};    // the label's top-left
};

struct Geometry {
    float s = 1.f;  // 1080p units -> pixels
    Box panel, icon, text;
    float wrap = 0.f, textSize = 0.f;
    Box list;  // lists: the box the rows sit in
    float rowH = 0.f;
    int rows = 0;
    bool scrollbar = false;
    bool thumbs = false;  // lists: a thumbnail column (some entry has an icon)
    Box bar;              // progress
    Box buttons[2];
    bool hasButton[2] = {false, false};
    Box hint;
    bool hintRow = false;
    HintItem enter, back;
    float hintSize = 0.f, glyphSize = 0.f;
};

ImVec2 TextSize(ImFont* f, float size, const char* s, float wrap = 0.f) {
    return f->CalcTextSizeA(size, FLT_MAX, wrap, s);
}

Geometry Compute(const Live& l, ImVec2 display) {
    Geometry g;
    const Request& r = l.request;
    const float s = std::max(0.1f, std::min(display.x / 1920.f, display.y / 1080.f));
    g.s = s;
    const bool list = r.kind == Kind::List, progress = r.kind == Kind::Progress;
    const float panelW = (list ? 1000.f : progress ? 820.f : 900.f) * s;
    const float pad = 40.f * s;
    g.textSize = 30.f * s;
    const bool icon = r.icon != Icon::None && !list;
    const float iconD = 76.f * s;
    const float textX = pad + (icon ? iconD + 30.f * s : 0.f);
    g.wrap = panelW - textX - pad;
    const std::string text = Utf8(r.text);
    const float textH = text.empty() ? 0.f : TextSize(g_font, g.textSize, text.c_str(), g.wrap).y;

    // Relative to the panel's top-left first; moved into place at the end.
    float y = pad;
    if (icon) g.icon = {{pad, y}, {pad + iconD, y + iconD}};
    const float block = std::max(icon ? iconD : 0.f, textH);
    const float ty = y + std::max(0.f, (block - textH) * 0.5f);
    g.text = {{textX, ty}, {textX + g.wrap, ty + textH}};
    y += block;
    if (list) {
        for (const Item& item : r.items) g.thumbs = g.thumbs || !item.iconPath.empty();
        g.rowH = (g.thumbs ? 96.f : 64.f) * s;
        g.rows = VisibleRows(r);
        g.scrollbar = static_cast<int>(r.items.size()) > g.rows;
        y += 26.f * s;
        g.list = {{pad, y}, {panelW - pad, y + g.rows * g.rowH + 2.f * s}};
        y = g.list.max.y;
    }
    if (progress) {
        y += 30.f * s;
        g.bar = {{pad, y}, {panelW - pad - 90.f * s, y + 22.f * s}};
        y = g.bar.max.y;
    }
    const bool b1 = HasButton(r, 1), b2 = HasButton(r, 2);
    // A list has no buttons, as on a PS4: Cross on an entry chooses it (button 1 with that
    // entry), Circle backs out, and the hint row names the action.
    if (!list && (b1 || b2)) {
        y += 40.f * s;
        const float h = 64.f * s, gap = 24.f * s;
        float w[2] = {0.f, 0.f};
        for (int i = 0; i < 2; ++i) {
            if (!(i == 0 ? b1 : b2)) continue;
            const std::string label = Utf8(i == 0 ? r.button1 : r.button2);
            w[i] = std::max(240.f * s, TextSize(g_bold, 28.f * s, label.c_str()).x + 80.f * s);
        }
        const float total = w[0] + w[1] + ((b1 && b2) ? gap : 0.f);
        float x = (panelW - total) * 0.5f;
        for (int i = 0; i < 2; ++i) {
            if (!(i == 0 ? b1 : b2)) continue;
            g.buttons[i] = {{std::round(x), std::round(y)}, {std::round(x + w[i]), std::round(y + h)}};
            g.hasButton[i] = true;
            x += w[i] + gap;
        }
        y += h;
    }
    g.hintRow = b1 || b2 || r.cancelButton != 0;
    y += pad;
    if (g.hintRow) {
        // Right-aligned: [Cross] Enter   [Circle] Back.
        const float rowH = 54.f * s;
        g.hint = {{0.f, y}, {panelW, y + rowH}};
        g.hintSize = 23.f * s;
        g.glyphSize = 24.f * s;
        const float gap = 10.f * s, between = 34.f * s, cy = y + rowH * 0.5f;
        float x = panelW - 34.f * s;
        const auto place = [&](HintItem& item, const std::string& label) {
            item.text = label;
            const ImVec2 ts = TextSize(g_font, g.hintSize, label.c_str());
            const float w = g.glyphSize + gap + ts.x;
            x -= w;
            item.present = true;
            item.hit = {{x - 8.f * s, y + 7.f * s}, {x + w + 8.f * s, y + rowH - 7.f * s}};
            item.glyph = {x + g.glyphSize * 0.5f, cy};
            item.label = {x + g.glyphSize + gap, cy - ts.y * 0.54f};
            x -= between;
        };
        if (r.cancelButton != 0) place(g.back, "Back");
        if (list ? b1 : (b1 || b2)) place(g.enter, list ? Utf8(r.button1) : std::string("Enter"));
        y += rowH;
    }
    const float panelH = y;
    const ImVec2 o{std::floor((display.x - panelW) * 0.5f), std::floor((display.y - panelH) * 0.5f)};
    const auto move = [&](Box& b) {
        b.min = {b.min.x + o.x, b.min.y + o.y};
        b.max = {b.max.x + o.x, b.max.y + o.y};
    };
    g.panel = {{0.f, 0.f}, {panelW, panelH}};
    for (Box* b : {&g.panel, &g.icon, &g.text, &g.list, &g.bar, &g.buttons[0], &g.buttons[1], &g.hint, &g.enter.hit,
                   &g.back.hit})
        move(*b);
    for (HintItem* h : {&g.enter, &g.back}) {
        h->glyph = {h->glyph.x + o.x, h->glyph.y + o.y};
        h->label = {h->label.x + o.x, h->label.y + o.y};
    }
    return g;
}

// ---- Drawing ----

constexpr ImU32 kBase = IM_COL32(18, 18, 18, 255);
constexpr ImU32 kLine = IM_COL32(70, 70, 70, 255);
constexpr ImU32 kButton = IM_COL32(60, 60, 60, 255);
constexpr ImU32 kButtonBorder = IM_COL32(80, 80, 80, 255);
constexpr ImU32 kListBg = IM_COL32(26, 26, 26, 255);
constexpr ImU32 kRow = IM_COL32(35, 35, 35, 255);
constexpr ImU32 kAccent = IM_COL32(30, 90, 170, 255);
constexpr ImU32 kAccentBorder = IM_COL32(21, 63, 119, 255);
constexpr ImU32 kAccentBright = IM_COL32(52, 128, 222, 255);
constexpr ImU32 kText = IM_COL32(230, 230, 230, 255);
constexpr ImU32 kDimText = IM_COL32(150, 150, 150, 255);
constexpr ImU32 kWhite = IM_COL32(248, 248, 248, 255);

ImU32 Lighter(ImU32 c, float boost) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.x = std::min(1.f, v.x + boost);
    v.y = std::min(1.f, v.y + boost);
    v.z = std::min(1.f, v.z + boost);
    return ImGui::ColorConvertFloat4ToU32(v);
}

void DrawIcon(ImDrawList* dl, const Box& b, Icon icon, float s) {
    const ImVec2 c = b.Centre();
    const float r = b.Size().x * 0.5f;
    switch (icon) {
    case Icon::Warning: {
        const ImU32 amber = IM_COL32(236, 172, 44, 255), ink = IM_COL32(28, 24, 18, 255);
        const ImVec2 top{c.x, c.y - r * 0.92f}, left{c.x - r, c.y + r * 0.82f}, right{c.x + r, c.y + r * 0.82f};
        dl->AddTriangleFilled(top, left, right, amber);
        dl->AddTriangle(top, left, right, amber, 4.f * s);  // softer corners
        const float w = r * 0.12f;
        dl->AddRectFilled({c.x - w, c.y - r * 0.40f}, {c.x + w, c.y + r * 0.28f}, ink, w);
        dl->AddCircleFilled({c.x, c.y + r * 0.53f}, w * 1.15f, ink);
        break;
    }
    case Icon::Error: {
        dl->AddCircleFilled(c, r, IM_COL32(206, 64, 64, 255));
        const float x = r * 0.36f, t = std::max(2.f, r * 0.16f);
        dl->AddLine({c.x - x, c.y - x}, {c.x + x, c.y + x}, kWhite, t);
        dl->AddLine({c.x + x, c.y - x}, {c.x - x, c.y + x}, kWhite, t);
        break;
    }
    case Icon::Question: {
        dl->AddCircleFilled(c, r, kAccentBright);
        const float size = r * 1.35f;
        const ImVec2 ts = TextSize(g_bold, size, "?");
        dl->AddText(g_bold, size, {c.x - ts.x * 0.5f, c.y - ts.y * 0.52f}, kWhite, "?");
        break;
    }
    case Icon::Info: {
        dl->AddCircleFilled(c, r, kAccentBright);
        const float w = r * 0.12f;
        dl->AddCircleFilled({c.x, c.y - r * 0.44f}, w * 1.25f, kWhite);
        dl->AddRectFilled({c.x - w, c.y - r * 0.16f}, {c.x + w, c.y + r * 0.56f}, kWhite, w);
        break;
    }
    default: break;
    }
}

// The PlayStation-style Cross and Circle as line shapes, centred on c, about d across.
void DrawButtonGlyph(ImDrawList* dl, ImVec2 c, float d, bool cross, ImU32 col, float t) {
    const float r = d * 0.5f;
    if (cross) {
        dl->AddLine({c.x - r * 0.72f, c.y - r * 0.72f}, {c.x + r * 0.72f, c.y + r * 0.72f}, col, t);
        dl->AddLine({c.x + r * 0.72f, c.y - r * 0.72f}, {c.x - r * 0.72f, c.y + r * 0.72f}, col, t);
    } else {
        dl->AddCircle(c, r * 0.82f, col, 0, t);
    }
}

void DrawButton(ImDrawList* dl, const Box& b, const std::wstring& label, bool focused, bool active, bool hover, float s) {
    const float round = 6.f * s;
    ImU32 bg = focused ? kAccent : kButton;
    if (hover) bg = Lighter(bg, 0.08f);
    dl->AddRectFilled(b.min, b.max, bg, round);
    if (focused && active) {
        dl->AddRect(b.min, b.max, kWhite, round, std::max(2.f, 3.f * s));
    } else {
        dl->AddRect(b.min, b.max, focused ? kAccentBorder : kButtonBorder, round, 1.f);
    }
    const std::string text = Utf8(label);
    const float size = 28.f * s;
    const ImVec2 ts = TextSize(g_bold, size, text.c_str());
    dl->AddText(g_bold, size, {b.min.x + (b.Size().x - ts.x) * 0.5f, b.min.y + (b.Size().y - ts.y) * 0.5f}, kText,
                text.c_str());
}

// A list entry's label: "<name>    <date>" draws the date on the right, dimmed.
void SplitLabel(const std::wstring& label, std::string& name, std::string& detail) {
    const size_t at = label.find(L"    ");
    if (at == std::wstring::npos) {
        name = Utf8(label);
        detail.clear();
        return;
    }
    name = Utf8(label.substr(0, at));
    size_t from = at;
    while (from < label.size() && label[from] == L' ') ++from;
    detail = Utf8(label.substr(from));
}

void DrawList(ImDrawList* dl, const Live& l, const Geometry& g, bool active, int hoverRow) {
    const float s = g.s;
    dl->AddRectFilled(g.list.min, g.list.max, kListBg, 4.f * s);
    dl->AddRect(g.list.min, g.list.max, kLine, 4.f * s, 1.f);
    const int n = static_cast<int>(l.request.items.size());
    const float sbW = g.scrollbar ? 10.f * s : 0.f;
    const float rowW = g.list.Size().x - 2.f * s - (g.scrollbar ? sbW + 10.f * s : 0.f);
    dl->PushClipRect(g.list.min, g.list.max, true);
    for (int i = 0; i < g.rows && l.firstRow + i < n; ++i) {
        const int idx = l.firstRow + i;
        const Item& item = l.request.items[idx];
        const float y = g.list.min.y + 1.f * s + i * g.rowH;
        const Box row{{g.list.min.x + 1.f * s, y}, {g.list.min.x + 1.f * s + rowW, y + g.rowH}};
        const bool sel = idx == l.selected;
        if (i > 0 && !sel && idx - 1 != l.selected)  // a faint separator
            dl->AddLine({row.min.x + 16.f * s, row.min.y}, {row.max.x - 16.f * s, row.min.y}, IM_COL32(44, 44, 44, 255));
        if (sel) {
            const Box in{{row.min.x + 4.f * s, row.min.y + 4.f * s}, {row.max.x - 4.f * s, row.max.y - 4.f * s}};
            dl->AddRectFilled(in.min, in.max, active ? IM_COL32(40, 66, 104, 255) : IM_COL32(46, 46, 46, 255), 4.f * s);
            if (active) dl->AddRect(in.min, in.max, kWhite, 4.f * s, std::max(2.f, 2.5f * s));
        } else if (idx == hoverRow) {
            dl->AddRectFilled({row.min.x + 4.f * s, row.min.y + 4.f * s}, {row.max.x - 4.f * s, row.max.y - 4.f * s}, kRow,
                              4.f * s);
        }
        float x = row.min.x + 24.f * s;
        if (g.thumbs) {
            const float d = g.rowH - 22.f * s;
            const Box ib{{x, row.Centre().y - d * 0.5f}, {x + d, row.Centre().y + d * 0.5f}};
            const SDL_Texture* tex = idx < static_cast<int>(l.icons.size()) ? l.icons[idx] : nullptr;
            if (tex) {
                // Keep the picture's shape: fit it in the square.
                float tw = 0.f, th = 0.f;
                SDL_GetTextureSize(const_cast<SDL_Texture*>(tex), &tw, &th);
                Box fit = ib;
                if (tw > 0.f && th > 0.f) {
                    const float k = std::min(d / tw, d / th);
                    const float w = tw * k, h = th * k;
                    fit = {{ib.Centre().x - w * 0.5f, ib.Centre().y - h * 0.5f}, {ib.Centre().x + w * 0.5f, ib.Centre().y + h * 0.5f}};
                }
                dl->AddRectFilled(ib.min, ib.max, IM_COL32(12, 12, 12, 255), 3.f * s);
                dl->AddImage(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(tex))), fit.min, fit.max);
            } else {
                dl->AddRectFilled(ib.min, ib.max, IM_COL32(32, 32, 32, 255), 3.f * s);
            }
            dl->AddRect(ib.min, ib.max, kLine, 3.f * s, 1.f);
            x = ib.max.x + 22.f * s;
        }
        std::string name, detail;
        SplitLabel(item.label, name, detail);
        const float size = 28.f * s, dsize = 22.f * s;
        float nameMax = row.max.x - 24.f * s - x;
        if (!detail.empty()) {
            const ImVec2 ds = TextSize(g_font, dsize, detail.c_str());
            dl->AddText(g_font, dsize, {row.max.x - 24.f * s - ds.x, row.Centre().y - ds.y * 0.5f},
                        sel && active ? kText : kDimText, detail.c_str());
            nameMax -= ds.x + 24.f * s;
        }
        const ImVec2 ns = TextSize(g_font, size, name.c_str());
        dl->PushClipRect({x, row.min.y}, {x + std::max(0.f, nameMax), row.max.y}, true);
        dl->AddText(g_font, size, {x, row.Centre().y - ns.y * 0.52f}, sel && active ? kWhite : kText, name.c_str());
        dl->PopClipRect();
    }
    dl->PopClipRect();
    if (g.scrollbar) {  // a track and a thumb for the visible part
        const float x1 = g.list.max.x - 8.f * s, x0 = x1 - sbW;
        const float top = g.list.min.y + 8.f * s, bottom = g.list.max.y - 8.f * s, span = bottom - top;
        dl->AddRectFilled({x0, top}, {x1, bottom}, IM_COL32(40, 40, 40, 255), sbW * 0.5f);
        const float t0 = top + span * l.firstRow / n, t1 = top + span * std::min(n, l.firstRow + g.rows) / n;
        dl->AddRectFilled({x0, t0}, {x1, t1}, IM_COL32(128, 128, 128, 255), sbW * 0.5f);
    }
}

void DrawHint(ImDrawList* dl, const Geometry& g, bool active, bool hoverBack, bool hoverEnter) {
    const float s = g.s;
    dl->AddLine({g.panel.min.x, g.hint.min.y}, {g.panel.max.x, g.hint.min.y}, kLine);
    const ImU32 col = active ? kText : kDimText;
    const float t = std::max(1.5f, 2.2f * s);
    if (g.back.present) {
        if (hoverBack) dl->AddRectFilled(g.back.hit.min, g.back.hit.max, IM_COL32(44, 44, 44, 255), 4.f * s);
        DrawButtonGlyph(dl, g.back.glyph, g.glyphSize, false, col, t);
        dl->AddText(g_font, g.hintSize, g.back.label, col, g.back.text.c_str());
    }
    if (g.enter.present) {
        if (hoverEnter) dl->AddRectFilled(g.enter.hit.min, g.enter.hit.max, IM_COL32(44, 44, 44, 255), 4.f * s);
        DrawButtonGlyph(dl, g.enter.glyph, g.glyphSize, true, col, t);
        dl->AddText(g_font, g.hintSize, g.enter.label, col, g.enter.text.c_str());
    }
}

void DrawProgress(ImDrawList* dl, const Live& l, const Geometry& g) {
    const float s = g.s;
    const float round = g.bar.Size().y * 0.5f;
    dl->AddRectFilled(g.bar.min, g.bar.max, IM_COL32(35, 35, 35, 255), round);
    const float w = g.bar.Size().x * std::min<uint32_t>(l.progress, 100) / 100.f;
    if (w > 0.f) dl->AddRectFilled(g.bar.min, {g.bar.min.x + std::max(w, round * 2.f), g.bar.max.y}, kAccentBright, round);
    dl->AddRect(g.bar.min, g.bar.max, kLine, round, 1.f);
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%u%%", std::min<uint32_t>(l.progress, 100));
    const float size = 26.f * s;
    const ImVec2 ts = TextSize(g_font, size, pct);
    dl->AddText(g_font, size, {g.panel.max.x - 40.f * s - ts.x, g.bar.Centre().y - ts.y * 0.52f}, kText, pct);
}

struct Hover {
    int button = 0;  // 1, 2 or 0
    int row = -1;    // list entry under the pointer
    bool back = false;
    bool enter = false;
};

// The top dialog's geometry as last drawn (dlgtest's hit points).
Geometry g_lastTop;
uint64_t g_lastTopId = 0;
int g_lastFirstRow = 0;

void DrawDialog(const Live& l, const Geometry& g, bool active, const Hover& hover) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = g.s;
    for (int i = 3; i >= 1; --i) {  // a soft shadow, as the keyboard's
        const float o = i * 3.f * s;
        dl->AddRectFilled({g.panel.min.x - o, g.panel.min.y - o + 2.f * s}, {g.panel.max.x + o, g.panel.max.y + o + 4.f * s},
                          IM_COL32(0, 0, 0, 30), 6.f * s + o);
    }
    dl->AddRectFilled(g.panel.min, g.panel.max, kBase, 4.f * s);
    dl->AddRect(g.panel.min, g.panel.max, kLine, 4.f * s, 1.f);
    if (l.request.icon != Icon::None && l.request.kind != Kind::List) DrawIcon(dl, g.icon, l.request.icon, s);
    const std::string text = Utf8(l.request.text);
    if (!text.empty()) dl->AddText(g_font, g.textSize, g.text.min, kText, text.c_str(), nullptr, g.wrap);
    if (l.request.kind == Kind::List) DrawList(dl, l, g, active, hover.row);
    if (l.request.kind == Kind::Progress) DrawProgress(dl, l, g);
    const int focused = FocusedButton(l);
    for (int i = 0; i < 2; ++i) {
        if (!g.hasButton[i]) continue;
        DrawButton(dl, g.buttons[i], i == 0 ? l.request.button1 : l.request.button2, focused == i + 1, active,
                   hover.button == i + 1, s);
    }
    if (g.hintRow) DrawHint(dl, g, active, hover.back, hover.enter);
}

// The mouse on the top dialog: hovering moves the focus or the selection when the pointer
// moves; a click presses a button or picks an entry (twice, or then Cross, chooses it); the
// wheel scrolls a list; a click on the Circle hint cancels. True once answered.
bool Mouse(Live& l, const Geometry& g, Hover& hover) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 m = io.MousePos;
    if (!ImGui::IsMousePosValid(&m)) return false;
    const bool moved = l.mouseSeen && (m.x != l.lastMouse.x || m.y != l.lastMouse.y);
    l.lastMouse = m;
    l.mouseSeen = true;
    const bool click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    for (int i = 0; i < 2; ++i) {
        if (!g.hasButton[i] || !g.buttons[i].Contains(m)) continue;
        hover.button = i + 1;
        if ((moved || click) && l.request.kind != Kind::List) MoveFocus(l, i + 1);
        if (click) return Press(l, i + 1);
    }
    if (l.request.kind == Kind::List && g.list.Contains(m)) {
        const int n = static_cast<int>(l.request.items.size());
        if (io.MouseWheel != 0.f)
            l.firstRow = std::clamp(l.firstRow - static_cast<int>(io.MouseWheel), 0, std::max(0, n - g.rows));
        const int row = l.firstRow + static_cast<int>((m.y - g.list.min.y) / g.rowH);
        if (row >= 0 && row < n && row < l.firstRow + g.rows) {
            hover.row = row;
            if (click) {
                const uint64_t now = NowMs();
                const bool twice = row == l.lastClickRow && now - l.lastClickMs <= kDoubleClickMs;
                l.selected = row;
                EnsureVisible(l);
                l.lastClickRow = row;
                l.lastClickMs = now;
                if (twice) return Press(l, 1);
            }
        }
    }
    if (g.back.present && g.back.hit.Contains(m)) {
        hover.back = true;
        if (click) return Press(l, l.request.cancelButton);
    }
    if (g.enter.present && g.enter.hit.Contains(m)) {  // a list's "Save": the selected entry
        hover.enter = true;
        if (click) return Press(l, FocusedButton(l));
    }
    return false;
}

}  // namespace

// ---- Interface ----

bool Available() { return ui::overlay::Available(); }

uint64_t Show(Request request, AnswerFn onAnswer) {
    if (request.defaultButton != 1 && request.defaultButton != 2) request.defaultButton = 1;
    std::lock_guard lock(g_mutex);
    const uint64_t id = g_nextId++;
    std::string buttons;
    if (!request.button1.empty()) buttons += Utf8(request.button1);
    if (!request.button2.empty()) buttons += (buttons.empty() ? "" : " / ") + Utf8(request.button2);
    std::fprintf(stderr, "[dlg] #%llu requested (%s): %s \"%s\"%s%s%s\n", static_cast<unsigned long long>(id),
                 request.logName ? request.logName : "dialog", KindName(request.kind), Short(request.text).c_str(),
                 buttons.empty() ? "" : " [", buttons.c_str(), buttons.empty() ? "" : "]");
    g_pending.push_back({id, host::NextStamp(), std::move(request), std::move(onAnswer)});
    g_open.push_back(id);
    g_openCount = static_cast<int>(g_open.size());
    return id;
}

void SetProgress(uint64_t id, uint32_t percent) {
    std::lock_guard lock(g_mutex);
    if (std::find(g_open.begin(), g_open.end(), id) == g_open.end()) return;
    for (Pending& p : g_pending) {
        if (p.id != id) continue;
        p.request.progress = percent;
        return;
    }
    g_progressQueue.emplace_back(id, percent);
}

void Close(uint64_t id) {
    std::lock_guard lock(g_mutex);
    const auto it = std::find(g_open.begin(), g_open.end(), id);
    if (id == 0 || it == g_open.end()) return;
    g_open.erase(it);
    g_openCount = static_cast<int>(g_open.size());
    g_pending.erase(std::remove_if(g_pending.begin(), g_pending.end(), [&](const Pending& p) { return p.id == id; }),
                    g_pending.end());
    std::fprintf(stderr, "[dlg] #%llu closed without an answer\n", static_cast<unsigned long long>(id));
}

bool Visible() { return g_openCount.load() != 0; }

// ---- Layer (ui/overlay_layers.h) ----

namespace layer {

bool Wanted() { return g_openCount.load() != 0 || !g_live.empty(); }

uint64_t Sync() { return ui::dialog::Sync(); }

bool Poll(bool top) {
    if (g_live.empty()) return false;
    for (size_t i = 0; i + 1 < g_live.size(); ++i) g_live[i].wasTop = false;
    Live& l = g_live.back();
    if (!top) {
        l.wasTop = false;
        return true;
    }
    if (!l.wasTop) {  // back on top: the press that answered the one above isn't ours
        Rearm(l);
        l.wasTop = true;
    }
    if (l.request.padIgnored) return true;  // LBP3_AUTOPRESS: the caller answers
    return PollPad(l);
}

bool Event(const SDL_Event& e) {
    if (g_live.empty()) return false;
    Live& l = g_live.back();
    const bool list = l.request.kind == Kind::List;
    switch (e.type) {
    case SDL_EVENT_TEXT_INPUT: return true;  // nothing to type into
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode key = e.key.key;
        if (key >= SDLK_F1 && key <= SDLK_F12) return false;  // the debug hotkeys keep working
        switch (key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
            if (e.key.repeat || l.enterHeld) return true;  // still held from the game: not an answer
            Press(l, FocusedButton(l));
            return true;  // the press may have ended the dialog: `l` is no longer valid
        case SDLK_ESCAPE:
            if (e.key.repeat || l.escHeld) return true;
            Press(l, l.request.cancelButton);
            return true;
        default: break;
        }
        // Navigation: not while the key is still held from before the dialog came up.
        if (std::find(l.navHeld.begin(), l.navHeld.end(), e.key.scancode) != l.navHeld.end()) return true;
        switch (key) {
        case SDLK_UP:
            if (list) {
                MoveSelection(l, -1);
            } else {
                MoveFocus(l, 1);
            }
            break;
        case SDLK_DOWN:
            if (list) {
                MoveSelection(l, 1);
            } else {
                MoveFocus(l, 2);
            }
            break;
        case SDLK_LEFT:
            if (!list) MoveFocus(l, 1);
            break;
        case SDLK_RIGHT:
            if (!list) MoveFocus(l, 2);
            break;
        case SDLK_TAB:
            if (!list) MoveFocus(l, l.focus == 1 ? 2 : 1);
            break;
        case SDLK_HOME:
            if (list) MoveSelection(l, -static_cast<int>(l.request.items.size()));
            break;
        case SDLK_END:
            if (list) MoveSelection(l, static_cast<int>(l.request.items.size()));
            break;
        case SDLK_PAGEUP:
            if (list) MoveSelection(l, -VisibleRows(l.request));
            break;
        case SDLK_PAGEDOWN:
            if (list) MoveSelection(l, VisibleRows(l.request));
            break;
        default: break;
        }
        return true;
    }
    case SDL_EVENT_KEY_UP:
        if (e.key.key >= SDLK_F1 && e.key.key <= SDLK_F12) return false;
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER || e.key.key == SDLK_SPACE) l.enterHeld = false;
        if (e.key.key == SDLK_ESCAPE) l.escHeld = false;
        std::erase(l.navHeld, e.key.scancode);
        return true;
    default: return false;
    }
}

void Frame(bool top, ImVec2 display) {
    if (g_live.empty()) return;
    g_font = host::Font();
    g_bold = host::KeyFont();
    // Older dialogs first, inactive and dimmed; then the newest, which takes the mouse when on top.
    for (size_t i = 0; i + 1 < g_live.size(); ++i) {
        DrawDialog(g_live[i], Compute(g_live[i], display), false, Hover{});
        ImGui::GetForegroundDrawList()->AddRectFilled({0.f, 0.f}, display, host::kDim);
    }
    Live& l = g_live.back();
    Hover hover;
    if (top && Mouse(l, Compute(l, display), hover)) return;  // answered: it's gone
    const Geometry g = Compute(l, display);
    g_lastTop = g;
    g_lastTopId = l.id;
    g_lastFirstRow = l.firstRow;
    DrawDialog(l, g, top, hover);
}

void Reset() {
    // A new window: the dialogs that are up go back to pending, so they come up again on it
    // (their icons are loaded again for its renderer) and still get answered.
    std::vector<Pending> back;
    for (Live& l : g_live) {
        Request r = l.request;
        r.progress = l.progress;
        r.focus = l.selected >= 0 ? l.selected : r.focus;
        back.push_back({l.id, l.stamp, std::move(r), std::move(l.onAnswer)});
    }
    while (!g_live.empty()) EndLive(g_live.size() - 1);
    std::lock_guard lock(g_mutex);
    g_pending.insert(g_pending.begin(), std::make_move_iterator(back.begin()), std::make_move_iterator(back.end()));
}

}  // namespace layer

// ---- Test hooks ----

namespace testing {

bool Top(State& out) {
    ui::dialog::Sync();
    if (g_live.empty()) return false;
    const Live& l = g_live.back();
    out.id = l.id;
    out.focus = l.focus;
    out.selected = l.selected;
    out.firstRow = l.firstRow;
    out.progress = l.progress;
    return true;
}

size_t LiveCount() { return g_live.size(); }

namespace {
bool Drawn() { return !g_live.empty() && g_lastTopId == g_live.back().id; }
}  // namespace

bool ButtonCentre(int button, float& x, float& y) {
    if (!Drawn() || button < 1 || button > 2 || !g_lastTop.hasButton[button - 1]) return false;
    const ImVec2 c = g_lastTop.buttons[button - 1].Centre();
    x = c.x;
    y = c.y;
    return true;
}

bool RowCentre(int index, float& x, float& y) {
    if (!Drawn() || g_lastTop.rows == 0) return false;
    const int i = index - g_lastFirstRow;
    if (i < 0 || i >= g_lastTop.rows) return false;
    x = g_lastTop.list.Centre().x;
    y = g_lastTop.list.min.y + (i + 0.5f) * g_lastTop.rowH;
    return true;
}

bool EnterHintCentre(float& x, float& y) {
    if (!Drawn() || !g_lastTop.enter.present) return false;
    const ImVec2 c = g_lastTop.enter.hit.Centre();
    x = c.x;
    y = c.y;
    return true;
}

bool BackHintCentre(float& x, float& y) {
    if (!Drawn() || !g_lastTop.back.present) return false;
    const ImVec2 c = g_lastTop.back.hit.Centre();
    x = c.x;
    y = c.y;
    return true;
}

void SetPad(const ps4::PadData* pad) { g_testPad = pad; }

void SetClock(uint64_t (*nowMs)()) { g_testClock = nowMs; }

}  // namespace testing

}  // namespace ui::dialog
