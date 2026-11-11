// The on-screen keyboard over the game window (see ui/osk_overlay.h), a layer of the overlay
// host (ui/overlay.cpp), which owns ImGui. State that any thread touches (the requested
// keyboard, its id) is under g_mutex; everything else belongs to the presenter thread.
//
// The look follows the PS4 keyboard as shadPS4 measured it (ime_kb_layout.cpp): a 793x528
// panel at 1080p with a title row, the text field, an empty prediction bar with a close
// button, and a 10-column key grid; key colours, the selection highlight (a lighter key
// with a white border) and the hotkey badges in each key's top-left corner come from there
// too. Button and key icons are drawn as shapes.
#include "ui/osk_overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "hle/pad_types.h"
#include "input/gamepad.h"
#include "ui/osk_model.h"
#include "ui/osk_testing.h"
#include "ui/overlay.h"
#include "ui/overlay_layers.h"

namespace ui::osk {
namespace {

namespace host = ui::overlay::detail;

// ---- Shared between threads (g_mutex) ----

struct Pending {
    uint64_t id = 0;
    uint64_t stamp = 0;  // show order among all overlay layers
    Request request;
    AnswerFn onAnswer;
};

std::mutex g_mutex;
std::optional<Pending> g_pending;    // shown, not yet picked up by the presenter
uint64_t g_nextId = 1;
std::atomic<uint64_t> g_current{0};  // the keyboard that should be up; 0 = none

// ---- Presenter thread ----

ImFont* g_font = nullptr;      // the host's fonts, refreshed each frame
ImFont* g_fontKeys = nullptr;
const ps4::PadData* g_testPad = nullptr;
uint64_t (*g_testClock)() = nullptr;

uint64_t NowMs() { return g_testClock ? g_testClock() : SDL_GetTicks(); }

constexpr uint64_t kBlinkMs = 530;
constexpr int kStickThreshold = 64;  // of 128: a clear push
constexpr int kStickCentre = 40;     // back inside this = released

struct Live {
    explicit Live(Pending p)
        : id(p.id), stamp(p.stamp), request(std::move(p.request)), onAnswer(std::move(p.onAnswer)),
          kb(request.text, request.maxLength, request.multiline) {}

    uint64_t id, stamp;
    bool wasTop = true;  // false while a newer overlay (a system dialog) has the input
    Request request;
    AnswerFn onAnswer;
    Keyboard kb;
    // Pad: buttons held when the keyboard came up count only after a release.
    uint32_t ignore = 0, prev = 0;
    bool stickIgnored = false;
    Repeater up, down, left, right, cross, square, triangle, l1, r1;
    bool l2Down = false, l2Combo = false, triangleCombo = false;
    // Physical keys held when it came up (the game's Confirm/Back) count only after a release.
    bool enterHeld = false, escHeld = false;
    // Mouse: hover selects only when the pointer moves, so it doesn't fight the pad.
    ImVec2 lastMouse{-1.f, -1.f};
    bool mouseSeen = false;  // the first frame only notes where the pointer is
    // The caret blink restarts on every edit or cursor move; the field scrolls to the caret.
    uint64_t lastEdits = ~0ull, blinkFrom = 0;
    float scrollX = 0.f;
    int scrollLine = 0;
};

std::optional<Live> g_live;

std::string Quoted(const std::u16string& s) { return "\"" + Utf8(s) + "\""; }

// ---- Lifecycle ----

bool ReadPad(ps4::PadData& pad) {
    if (g_testPad) {
        pad = *g_testPad;
        return true;
    }
    return input::Read(0, pad, false);
}

void EndLive() {
    if (!g_live) return;
    if (SDL_Window* w = host::Window()) SDL_StopTextInput(w);
    // The release of a click that answered comes after the keyboard is gone.
    host::ReleaseMouse();
    g_live.reset();
}

// Whatever is held now (buttons, the stick, Enter and Esc) counts only after a release: when
// the keyboard comes up, and when it is back on top after a system dialog closed over it.
void Rearm(Live& l) {
    ps4::PadData pad{};
    const bool ok = ReadPad(pad);
    l.ignore = ok ? pad.buttons : 0;
    l.prev = 0;
    l.stickIgnored = ok && (std::abs(int(pad.leftStick.x) - 128) > kStickCentre ||
                            std::abs(int(pad.leftStick.y) - 128) > kStickCentre);
    for (Repeater* r : {&l.up, &l.down, &l.left, &l.right, &l.cross, &l.square, &l.triangle, &l.l1, &l.r1}) r->Reset();
    l.l2Down = l.l2Combo = l.triangleCombo = false;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    l.enterHeld = keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]);
    l.escHeld = keys && keys[SDL_SCANCODE_ESCAPE];
}

void StartLive(Pending p) {
    host::EnsureImGui();
    g_live.emplace(std::move(p));
    Live& l = *g_live;
    Rearm(l);
    if (SDL_Window* w = host::Window()) SDL_StartTextInput(w);
    std::fprintf(stderr, "[osk] keyboard #%llu up: %s, up to %u, text %s\n", static_cast<unsigned long long>(l.id),
                 Quoted(l.request.title).c_str(), l.request.maxLength, Quoted(l.request.text).c_str());
}

// Picks up Show and Close from other threads. Returns the stamp of the keyboard that is up.
uint64_t Sync() {
    std::optional<Pending> take;
    uint64_t want;
    {
        std::lock_guard lock(g_mutex);
        want = g_current.load();
        if (g_pending) {
            take = std::move(g_pending);
            g_pending.reset();
        }
    }
    if (g_live && g_live->id != want) EndLive();  // closed or replaced
    if (take && take->id == want) StartLive(std::move(*take));
    return g_live ? g_live->stamp : 0;
}

// The player is done: take the keyboard down, then answer (unless Close got there first).
void Finish(bool confirmed) {
    if (!g_live) return;
    ui::osk::Answer answer;
    answer.confirmed = confirmed;
    if (confirmed) answer.text = g_live->kb.text();
    AnswerFn fn = std::move(g_live->onAnswer);
    const uint64_t id = g_live->id;
    bool current;
    {
        std::lock_guard lock(g_mutex);
        current = g_current.load() == id;
        if (current) g_current = 0;
    }
    EndLive();
    if (!current) return;
    if (confirmed) {
        std::fprintf(stderr, "[osk] keyboard #%llu confirmed: %s (%zu units)\n", static_cast<unsigned long long>(id),
                     Quoted(answer.text).c_str(), answer.text.size());
    } else {
        std::fprintf(stderr, "[osk] keyboard #%llu cancelled\n", static_cast<unsigned long long>(id));
    }
    if (fn) fn(answer);
}

// ---- Pad ----

// False once the keyboard has answered.
bool PollPad() {
    Live& l = *g_live;
    ps4::PadData pad{};
    const bool ok = ReadPad(pad);
    const uint32_t raw = ok ? pad.buttons : 0;
    l.ignore &= raw;  // released buttons stop being ignored
    const uint32_t b = raw & ~l.ignore;
    const auto pressed = [&](uint32_t mask) { return (b & mask) && !(l.prev & mask); };
    const auto released = [&](uint32_t mask) { return !(b & mask) && (l.prev & mask); };

    // The left stick as a d-pad: its dominant axis, once clearly pushed.
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
    Keyboard& kb = l.kb;
    if (int f = l.up.Update((b & ps4::PAD_UP) || sUp, now)) kb.Move(-1, 0, f == 2);
    if (int f = l.down.Update((b & ps4::PAD_DOWN) || sDown, now)) kb.Move(1, 0, f == 2);
    if (int f = l.left.Update((b & ps4::PAD_LEFT) || sLeft, now)) kb.Move(0, -1, f == 2);
    if (int f = l.right.Update((b & ps4::PAD_RIGHT) || sRight, now)) kb.Move(0, 1, f == 2);

    if (pressed(ps4::PAD_CIRCLE)) {
        l.prev = b;
        Finish(false);
        return false;
    }
    if (pressed(ps4::PAD_R2)) {
        l.prev = b;
        Finish(true);
        return false;
    }
    // Cross (or a touch-pad click) presses the selected key; editing keys repeat when held.
    if (int f = l.cross.Update(b & (ps4::PAD_CROSS | ps4::PAD_TOUCH_PAD), now)) {
        const Action a = kb.selected()->action;
        const bool repeats =
            a == Action::Backspace || a == Action::Space || a == Action::ArrowLeft || a == Action::ArrowRight;
        if ((f == 1 || repeats) && kb.Activate() == Outcome::Done) {
            l.prev = b;
            Finish(true);
            return false;
        }
    }
    if (l.square.Update(b & ps4::PAD_SQUARE, now)) kb.Backspace();
    if (l.l1.Update(b & ps4::PAD_L1, now)) kb.CursorLeft();
    if (l.r1.Update(b & ps4::PAD_R1, now)) kb.CursorRight();
    // L2 is Shift on release, unless Triangle was pressed while it was held: L2+Triangle
    // switches to the symbols instead.
    if (pressed(ps4::PAD_L2)) {
        l.l2Down = true;
        l.l2Combo = false;
    }
    if (pressed(ps4::PAD_TRIANGLE) && l.l2Down) {
        kb.ToggleSymbols();
        l.l2Combo = l.triangleCombo = true;
    }
    if (!(b & ps4::PAD_TRIANGLE)) l.triangleCombo = false;
    if (l.triangle.Update((b & ps4::PAD_TRIANGLE) && !l.triangleCombo, now)) kb.Space();
    if (released(ps4::PAD_L2) && l.l2Down) {
        if (!l.l2Combo) kb.Shift();
        l.l2Down = false;
    }
    if (pressed(ps4::PAD_L3)) kb.ToggleSpecials();
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

struct KeyBox {
    const KeySpec* key;
    Box box;
};

struct Geometry {
    float s = 1.f;  // 1080p units -> pixels
    Box panel, title, input, predict, close, keys;
    std::vector<KeyBox> boxes;  // the drawn keys
};

Geometry Compute(const Keyboard& kb, ImVec2 display) {
    Geometry g;
    const float s = std::max(0.1f, std::min(display.x / 1920.f, display.y / 1080.f));
    g.s = s;
    const bool multi = kb.multiline();
    const float panelW = 793.f * s, panelH = (multi ? 628.f : 528.f) * s;
    const float x = std::floor((display.x - panelW) * 0.5f), y = std::floor((display.y - panelH) * 0.5f);
    g.panel = {{x, y}, {x + panelW, y + panelH}};
    const float labelH = 57.f * s, inputH = (multi ? 151.f : 50.f) * s, predictH = 53.f * s, closeW = 53.f * s;
    const float keysH = 316.f * s, padX = 26.f * s, padBottom = 26.f * s;
    const float gapV = std::max(0.f, (panelH - (labelH + inputH + predictH + keysH + padBottom)) * 0.5f);
    g.title = {{x + padX, y}, {x + panelW - padX, y + labelH}};
    g.input = {{x + padX, y + labelH}, {x + panelW - padX, y + labelH + inputH}};
    const float py = y + labelH + inputH + gapV;
    g.predict = {{x, py}, {x + 740.f * s, py + predictH}};
    g.close = {{x + panelW - closeW, py}, {x + panelW, py + predictH}};
    const float ky = py + predictH + gapV;
    g.keys = {{x + padX, ky}, {x + panelW - padX, ky + keysH}};

    const Layout& lay = kb.layout();
    const float gap = 9.f * s;
    const float keyW = (g.keys.Size().x - gap * (lay.cols - 1)) / lay.cols;
    const float h6 = (keysH - gap * 5.f) / 6.f;  // the function rows keep the 6-row height
    const int typingRows = lay.rows - lay.functionRows;
    const float typingH =
        lay.rows == 6 ? h6 : (keysH - gap * (lay.rows - 1) - h6 * lay.functionRows) / std::max(1, typingRows);
    std::vector<float> rowY(lay.rows), rowH(lay.rows);
    float cy = ky;
    for (int r = 0; r < lay.rows; ++r) {
        rowH[r] = r < typingRows ? typingH : h6;
        rowY[r] = cy;
        cy += rowH[r] + gap;
    }
    for (size_t i = 0; i < lay.count; ++i) {
        const KeySpec& k = lay.keys[i];
        if (!Enabled(&k) || KeyAt(lay, k.row, k.col) != &k) continue;
        const int cs = std::max<int>(1, k.colSpan), rs = std::max<int>(1, k.rowSpan);
        const int lastRow = std::min<int>(lay.rows - 1, k.row + rs - 1);
        const float kx = g.keys.min.x + k.col * (keyW + gap);
        const Box b{{std::round(kx), std::round(rowY[k.row])},
                    {std::round(kx + cs * keyW + (cs - 1) * gap), std::round(rowY[lastRow] + rowH[lastRow])}};
        g.boxes.push_back({&k, b});
    }
    return g;
}

// ---- Drawing ----

constexpr ImU32 kBase = IM_COL32(18, 18, 18, 255);
constexpr ImU32 kLine = IM_COL32(70, 70, 70, 255);
constexpr ImU32 kTextField = IM_COL32(22, 37, 60, 255);
constexpr ImU32 kPreedit = IM_COL32(35, 35, 35, 255);
constexpr ImU32 kKeyDefault = IM_COL32(35, 35, 35, 255);
constexpr ImU32 kKeyFunction = IM_COL32(60, 60, 60, 255);
constexpr ImU32 kKeySymbol = IM_COL32(78, 78, 78, 255);
constexpr ImU32 kKeyBorder = IM_COL32(80, 80, 80, 255);
constexpr ImU32 kDone = IM_COL32(30, 90, 170, 255);
constexpr ImU32 kDoneBorder = IM_COL32(21, 63, 119, 255);
constexpr ImU32 kText = IM_COL32(230, 230, 230, 255);
constexpr ImU32 kHotkeyText = IM_COL32(220, 220, 220, 255);
constexpr ImU32 kDimText = IM_COL32(150, 150, 150, 255);
constexpr ImU32 kSelectedBorder = IM_COL32(248, 248, 248, 255);

ImU32 Lighter(ImU32 c, float boost) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.x = std::min(1.f, v.x + boost);
    v.y = std::min(1.f, v.y + boost);
    v.z = std::min(1.f, v.z + boost);
    return ImGui::ColorConvertFloat4ToU32(v);
}

ImVec2 TextSize(ImFont* f, float size, const char* s, const char* e = nullptr) {
    return f->CalcTextSizeA(size, 1e9f, 0.f, s, e);
}

// PlayStation-style button icons as line shapes, centred on c, about d across.
enum class Icon { Triangle, Square, Circle, Cross };
void DrawIcon(ImDrawList* dl, ImVec2 c, float d, Icon icon, ImU32 col, float t) {
    const float r = d * 0.5f;
    switch (icon) {
    case Icon::Triangle: {
        const float h = r * 0.93f;
        dl->AddTriangle({c.x, c.y - h}, {c.x - r, c.y + h * 0.78f}, {c.x + r, c.y + h * 0.78f}, col, t);
        break;
    }
    case Icon::Square:
        dl->AddRect({c.x - r * 0.8f, c.y - r * 0.8f}, {c.x + r * 0.8f, c.y + r * 0.8f}, col, 0.f, t);
        break;
    case Icon::Circle: dl->AddCircle(c, r * 0.85f, col, 0, t); break;
    case Icon::Cross:
        dl->AddLine({c.x - r * 0.75f, c.y - r * 0.75f}, {c.x + r * 0.75f, c.y + r * 0.75f}, col, t);
        dl->AddLine({c.x + r * 0.75f, c.y - r * 0.75f}, {c.x - r * 0.75f, c.y + r * 0.75f}, col, t);
        break;
    }
}

// A hotkey hint ("L2", "R2", "TRI", "L2+TRI"...) in a dark badge at the key's top-left.
void DrawHotkey(ImDrawList* dl, const Box& key, const char* hotkey, float s) {
    if (!hotkey || !*hotkey) return;
    struct Part {
        std::string text;
        bool icon = false;
        Icon which = Icon::Triangle;
    };
    std::vector<Part> parts;  // words are text, TRI/SQR are icons, joined by '+'
    const std::string h = hotkey;
    size_t at = 0;
    for (;;) {
        const size_t plus = h.find('+', at);
        const std::string word = h.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
        if (word == "TRI") {
            parts.push_back({"", true, Icon::Triangle});
        } else if (word == "SQR") {
            parts.push_back({"", true, Icon::Square});
        } else {
            parts.push_back({word});
        }
        if (plus == std::string::npos) break;
        parts.push_back({"+"});
        at = plus + 1;
    }
    const float size = std::max(8.f, 16.f * s);
    const float iconW = size * 0.78f;
    float w = 0.f;
    for (const Part& p : parts) w += p.icon ? iconW : TextSize(g_font, size, p.text.c_str()).x;
    const float padX = std::max(2.f, size * 0.3f), padY = std::max(1.f, size * 0.08f);
    const ImVec2 bmin{key.min.x + std::max(3.f, 4.f * s), key.min.y + std::max(2.f, 3.f * s)};
    const ImVec2 bmax{bmin.x + w + padX * 2.f, bmin.y + size + padY * 2.f};
    const float round = std::max(2.f, size * 0.24f);
    dl->AddRectFilled(bmin, bmax, IM_COL32(10, 10, 10, 140), round);
    dl->AddRect(bmin, bmax, IM_COL32(92, 92, 92, 110), round, 1.f);
    float x = bmin.x + padX;
    const float cy = (bmin.y + bmax.y) * 0.5f;
    for (const Part& p : parts) {
        if (p.icon) {
            DrawIcon(dl, {x + iconW * 0.5f, cy}, iconW * 0.92f, p.which, kHotkeyText, std::max(1.f, 1.4f * s));
            x += iconW;
        } else {
            dl->AddText(g_font, size, {x, cy - size * 0.5f - size * 0.06f}, kHotkeyText, p.text.c_str());
            x += TextSize(g_font, size, p.text.c_str()).x;
        }
    }
}

void DrawShiftArrow(ImDrawList* dl, ImVec2 c, float extent, bool filled, bool capsBar, ImU32 col, float t) {
    const float k = 0.88f, yOff = extent * 0.03f;
    const float tip = c.y + yOff - extent * 0.30f * k, shoulder = c.y + yOff - extent * 0.03f * k;
    const float base = c.y + yOff + extent * 0.21f * k;
    const float headW = extent * 0.30f * k, stemW = extent * 0.11f * k;
    const ImVec2 pts[7] = {{c.x, tip},          {c.x - headW, shoulder}, {c.x - stemW, shoulder}, {c.x - stemW, base},
                           {c.x + stemW, base}, {c.x + stemW, shoulder}, {c.x + headW, shoulder}};
    if (filled) {  // concave: the head and the stem as two convex pieces
        dl->AddTriangleFilled(pts[0], pts[1], pts[6], col);
        dl->AddRectFilled({c.x - stemW, shoulder - 0.5f}, {c.x + stemW, base}, col);
    } else {
        dl->AddPolyline(pts, 7, col, t, ImDrawFlags_Closed);
    }
    if (capsBar) {
        const float w = extent * 0.24f * k, h = std::max(t * 1.25f, extent * 0.07f);
        const float y = base + extent * 0.10f * k;
        dl->AddRectFilled({c.x - w * 0.5f, y - h * 0.5f}, {c.x + w * 0.5f, y + h * 0.5f}, col, h * 0.45f);
    }
}

void DrawGlyph(ImDrawList* dl, const Box& b, Glyph glyph, ImU32 col, float s) {
    const ImVec2 c = b.Centre();
    const float extent = std::min(b.Size().x, b.Size().y);
    const float t = std::max(1.2f, 2.0f * s);
    switch (glyph) {
    case Glyph::Backspace: {
        const float bodyW = extent * 0.56f, bodyH = extent * 0.42f, tipW = extent * 0.28f;
        const float left = c.x - (bodyW - tipW) * 0.5f, right = left + bodyW;
        const float top = c.y - bodyH * 0.5f, bottom = c.y + bodyH * 0.5f;
        const ImVec2 frame[5] = {{left, top}, {right, top}, {right, bottom}, {left, bottom}, {left - tipW, c.y}};
        dl->AddPolyline(frame, 5, col, t, ImDrawFlags_Closed);
        const float x = extent * 0.10f, cx = left + bodyW * 0.5f;
        dl->AddLine({cx - x, c.y - x}, {cx + x, c.y + x}, col, t);
        dl->AddLine({cx + x, c.y - x}, {cx - x, c.y + x}, col, t);
        break;
    }
    case Glyph::ArrowLeft:
    case Glyph::ArrowRight:
    case Glyph::ArrowUp:
    case Glyph::ArrowDown: {
        const float r = extent * 0.17f;
        ImVec2 a, p, q;
        switch (glyph) {
        case Glyph::ArrowLeft: a = {c.x - r, c.y}, p = {c.x + r * 0.7f, c.y - r}, q = {c.x + r * 0.7f, c.y + r}; break;
        case Glyph::ArrowRight: a = {c.x + r, c.y}, p = {c.x - r * 0.7f, c.y + r}, q = {c.x - r * 0.7f, c.y - r}; break;
        case Glyph::ArrowUp: a = {c.x, c.y - r}, p = {c.x + r, c.y + r * 0.7f}, q = {c.x - r, c.y + r * 0.7f}; break;
        default: a = {c.x, c.y + r}, p = {c.x - r, c.y - r * 0.7f}, q = {c.x + r, c.y - r * 0.7f}; break;
        }
        dl->AddTriangleFilled(a, p, q, col);
        break;
    }
    case Glyph::ShiftOutline: DrawShiftArrow(dl, c, extent, false, false, col, t); break;
    case Glyph::ShiftFilled: DrawShiftArrow(dl, c, extent, true, false, col, t); break;
    case Glyph::CapsLockFilled: DrawShiftArrow(dl, c, extent, true, true, col, t); break;
    default: break;
    }
}

const char* EnterLabel(uint32_t label) {
    switch (label) {
    case 1: return "Send";
    case 2: return "Search";
    case 3: return "Go";
    default: return "Done";
    }
}

void DrawKey(ImDrawList* dl, const Keyboard& kb, const KeyBox& kbx, bool selected, const Request& req, float s) {
    const KeySpec& k = *kbx.key;
    const Box& b = kbx.box;
    const bool done = k.action == Action::Done;
    ImU32 bg = kKeyDefault;
    if (done) {
        bg = kDone;
    } else if (k.action != Action::Character) {
        bg = kKeyFunction;
    } else if (kb.family() == Family::Symbols) {
        bg = kKeySymbol;
    }
    if (selected) bg = Lighter(bg, 0.11f);
    const float round = std::max(1.f, 793.f * s * 0.004f);
    dl->AddRectFilled(b.min, b.max, bg, round);
    const ImU32 border = selected ? kSelectedBorder : done ? kDoneBorder : kKeyBorder;
    dl->AddRect(b.min, b.max, border, round, selected ? std::max(2.f, 3.f * s) : 1.f);

    // The shift key shows the case: outline, filled (one letter), filled with a bar (caps lock).
    Glyph glyph = k.glyph;
    if (k.action == Action::Shift) {
        glyph = kb.caseState() == Case::Lower ? Glyph::ShiftOutline
                : kb.caseState() == Case::Upper ? Glyph::ShiftFilled
                                                : Glyph::CapsLockFilled;
    }
    const char* label = done ? EnterLabel(req.enterLabel) : k.label;
    if ((!label || !*label) && glyph != Glyph::None) {
        Box g = b;  // clear of the hint badge, like the labels
        if (k.hotkey) g.min.y += b.Size().y * 0.12f;
        DrawGlyph(dl, g, glyph, kText, s);
    }
    if (label && *label) {
        const bool typing = k.action == Action::Character;
        float size = (typing ? 34.f : 25.f) * s;
        if (b.Size().y < 40.f * s) size *= b.Size().y / (45.f * s);  // the 7-row pages' shorter keys
        ImVec2 ts = TextSize(g_fontKeys, size, label);
        const float maxW = b.Size().x - std::max(4.f, b.Size().x * 0.16f);
        if (ts.x > maxW && ts.x > 0.f) {
            size *= maxW / ts.x;
            ts = TextSize(g_fontKeys, size, label);
        }
        // Keys with a hint badge put their label a little lower, clear of it.
        const float yBias = k.hotkey ? b.Size().y * 0.08f : 0.f;
        dl->AddText(g_fontKeys, size, {b.min.x + (b.Size().x - ts.x) * 0.5f, b.min.y + (b.Size().y - ts.y) * 0.5f + yBias},
                    kText, label);
    }
    DrawHotkey(dl, b, k.hotkey, s);
}

void DrawTextField(ImDrawList* dl, Live& l, const Geometry& g, uint64_t now) {
    const float s = g.s;
    const Box& in = g.input;
    dl->AddRectFilled(in.min, in.max, kTextField, 2.f * s);
    dl->AddRect(in.min, in.max, IM_COL32(46, 72, 110, 255), 2.f * s, 1.f);
    const Keyboard& kb = l.kb;
    const bool multi = kb.multiline();
    const float size = (multi ? 30.f : 32.f) * s;
    const float padX = 12.f * s;
    const bool caretOn = ((now - l.blinkFrom) / kBlinkMs) % 2 == 0;
    std::u16string shown = kb.text();
    if (l.request.password) {
        for (char16_t& c : shown)
            if (c != u'\n') c = 0x2022;  // bullet
    }
    dl->PushClipRect(in.min, in.max, true);
    if (shown.empty() && !l.request.placeholder.empty()) {
        const std::string ph = Utf8(l.request.placeholder);
        const float y = multi ? in.min.y + 6.f * s : in.Centre().y - size * 0.62f;
        dl->AddText(g_font, size, {in.min.x + padX, y}, IM_COL32(120, 130, 145, 255), ph.c_str());
    }
    const size_t cursor = std::min(kb.cursor(), shown.size());
    const float visW = in.Size().x - padX * 2.f;
    const float caretT = std::max(1.5f, 2.f * s);
    if (!multi) {
        const std::string all = Utf8(shown), before = Utf8(std::u16string_view(shown).substr(0, cursor));
        const float caretX = TextSize(g_font, size, before.c_str()).x;
        const float total = TextSize(g_font, size, all.c_str()).x;
        if (caretX - l.scrollX > visW) l.scrollX = caretX - visW;
        if (caretX < l.scrollX) l.scrollX = caretX;
        if (total - l.scrollX < visW) l.scrollX = std::max(0.f, total - visW);
        const float x0 = in.min.x + padX - l.scrollX;
        dl->AddText(g_font, size, {x0, in.Centre().y - size * 0.62f}, kText, all.c_str());
        if (caretOn) {
            const float cx = std::round(x0 + caretX);
            dl->AddLine({cx, in.min.y + 9.f * s}, {cx, in.max.y - 9.f * s}, kText, caretT);
        }
    } else {
        // Lines, as many as fit, scrolled to keep the caret's line in view.
        std::vector<size_t> starts{0};
        for (size_t i = 0; i < shown.size(); ++i)
            if (shown[i] == u'\n') starts.push_back(i + 1);
        int caretLine = 0;
        for (size_t i = 0; i < starts.size(); ++i)
            if (starts[i] <= cursor) caretLine = int(i);
        const float lineH = size * 1.18f;
        const int visible = std::max(1, int((in.Size().y - 12.f * s) / lineH));
        if (caretLine < l.scrollLine) l.scrollLine = caretLine;
        if (caretLine >= l.scrollLine + visible) l.scrollLine = caretLine - visible + 1;
        for (int i = l.scrollLine; i < int(starts.size()) && i < l.scrollLine + visible; ++i) {
            const size_t end = i + 1 < int(starts.size()) ? starts[i + 1] - 1 : shown.size();
            const std::u16string_view view(shown);
            const std::string line = Utf8(view.substr(starts[i], end - starts[i]));
            const float y = in.min.y + 6.f * s + (i - l.scrollLine) * lineH;
            dl->AddText(g_font, size, {in.min.x + padX, y}, kText, line.c_str());
            if (i == caretLine && caretOn) {
                const std::string before = Utf8(view.substr(starts[i], cursor - starts[i]));
                const float cx = std::round(in.min.x + padX + TextSize(g_font, size, before.c_str()).x);
                dl->AddLine({cx, y + 2.f * s}, {cx, y + lineH - 2.f * s}, kText, caretT);
            }
        }
    }
    dl->PopClipRect();
}

void DrawPanel(Live& l, const Geometry& g, ImVec2 display, bool closeHover) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = g.s;
    const uint64_t now = NowMs();
    (void)display;  // the host dims the game behind all overlays
    for (int i = 3; i >= 1; --i) {  // a soft shadow
        const float o = i * 3.f * s;
        dl->AddRectFilled({g.panel.min.x - o, g.panel.min.y - o + 2.f * s},
                          {g.panel.max.x + o, g.panel.max.y + o + 4.f * s}, IM_COL32(0, 0, 0, 30), 6.f * s + o);
    }
    dl->AddRectFilled(g.panel.min, g.panel.max, kBase, 4.f * s);
    dl->AddRect(g.panel.min, g.panel.max, kLine, 4.f * s, 1.f);

    // The title and the length counter.
    const Keyboard& kb = l.kb;
    char counter[32];
    std::snprintf(counter, sizeof(counter), "%zu/%u", kb.text().size(), kb.maxLength());
    const float countSize = 19.f * s;
    const ImVec2 cs = TextSize(g_font, countSize, counter);
    dl->AddText(g_font, countSize, {g.title.max.x - cs.x, g.title.Centre().y - cs.y * 0.5f}, kDimText, counter);
    std::string title = l.request.title.empty() ? std::string("Enter text") : Utf8(l.request.title);
    const float titleSize = 26.f * s;
    const float titleMax = g.title.Size().x - cs.x - 24.f * s;
    if (TextSize(g_font, titleSize, title.c_str()).x > titleMax) {
        while (!title.empty() && TextSize(g_font, titleSize, (title + "...").c_str()).x > titleMax) {
            // One whole UTF-8 character: its continuation bytes, then its lead byte.
            while (!title.empty() && (static_cast<unsigned char>(title.back()) & 0xC0) == 0x80) title.pop_back();
            if (!title.empty()) title.pop_back();
        }
        title += "...";
    }
    const ImVec2 ts = TextSize(g_font, titleSize, title.c_str());
    dl->AddText(g_font, titleSize, {g.title.min.x, g.title.Centre().y - ts.y * 0.5f}, kText, title.c_str());

    DrawTextField(dl, l, g, now);

    // The prediction bar (no suggestions here) and the close button.
    dl->AddRectFilled(g.predict.min, g.predict.max, kPreedit);
    dl->AddLine({g.panel.min.x, g.predict.min.y}, {g.panel.max.x, g.predict.min.y}, kLine);
    dl->AddLine({g.panel.min.x, g.predict.max.y}, {g.panel.max.x, g.predict.max.y}, kLine);
    dl->AddRectFilled(g.close.min, g.close.max, closeHover ? Lighter(kKeyFunction, 0.11f) : kKeyFunction);
    dl->AddLine(g.close.min, {g.close.min.x, g.close.max.y}, kLine);
    DrawIcon(dl, g.close.Centre(), g.close.Size().y * 0.46f, Icon::Circle, kText, std::max(1.5f, 2.2f * s));

    const KeySpec* sel = kb.selected();
    for (const KeyBox& kbx : g.boxes)
        if (kbx.key != sel) DrawKey(dl, kb, kbx, false, l.request, s);
    for (const KeyBox& kbx : g.boxes)  // the selected key last, so its border sits on top
        if (kbx.key == sel) DrawKey(dl, kb, kbx, true, l.request, s);
}

}  // namespace

// ---- Interface ----

bool Available() { return ui::overlay::Available(); }

uint64_t Show(Request request, AnswerFn onAnswer) {
    request.maxLength = std::clamp<uint32_t>(request.maxLength, 1, 2048);
    std::lock_guard lock(g_mutex);
    const uint64_t id = g_nextId++;
    std::fprintf(stderr, "[osk] keyboard #%llu requested: %s, up to %u\n", static_cast<unsigned long long>(id),
                 Quoted(request.title).c_str(), request.maxLength);
    g_pending = Pending{id, host::NextStamp(), std::move(request), std::move(onAnswer)};
    g_current = id;
    return id;
}

void Close(uint64_t id) {
    std::lock_guard lock(g_mutex);
    if (id == 0 || g_current.load() != id) return;
    g_current = 0;
    if (g_pending && g_pending->id == id) g_pending.reset();
    std::fprintf(stderr, "[osk] keyboard #%llu closed without an answer\n", static_cast<unsigned long long>(id));
}

bool Visible() { return g_current.load() != 0; }

// The presenter's calls go to the host (ui/overlay.h); these stay for the test programs.
void Attach(SDL_Window* window, SDL_Renderer* renderer) { ui::overlay::Attach(window, renderer); }
bool HandleEvent(const SDL_Event& e) { return ui::overlay::HandleEvent(e); }
void Draw() { ui::overlay::Draw(); }

// ---- Layer (ui/overlay_layers.h) ----

namespace layer {

bool Wanted() { return g_current.load() != 0 || g_live.has_value(); }

uint64_t Sync() { return ui::osk::Sync(); }

bool Poll(bool top) {
    if (!g_live) return false;
    Live& l = *g_live;
    if (!top) {
        l.wasTop = false;
        return true;
    }
    if (!l.wasTop) {  // a system dialog over it just closed: its answering press isn't ours
        Rearm(l);
        l.wasTop = true;
    }
    return PollPad();
}

bool Event(const SDL_Event& e) {
    if (!g_live) return false;
    Live& l = *g_live;
    Keyboard& kb = l.kb;
    switch (e.type) {
    case SDL_EVENT_TEXT_INPUT:
        if (e.text.text) kb.Type(Utf16(e.text.text));
        return true;
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode key = e.key.key;
        if (key >= SDLK_F1 && key <= SDLK_F12) return false;  // the debug hotkeys keep working
        switch (key) {
        case SDLK_BACKSPACE: kb.Backspace(); break;
        case SDLK_DELETE: kb.Delete(); break;
        case SDLK_LEFT: kb.CursorLeft(); break;
        case SDLK_RIGHT: kb.CursorRight(); break;
        case SDLK_HOME: kb.CursorHome(); break;
        case SDLK_END: kb.CursorEnd(); break;
        case SDLK_UP:
            if (kb.multiline()) kb.CursorUp();
            break;
        case SDLK_DOWN:
            if (kb.multiline()) kb.CursorDown();
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (e.key.repeat || l.enterHeld) break;  // still held from the game: not an answer
            if (kb.multiline() && !(e.key.mod & SDL_KMOD_CTRL)) {
                kb.NewLine();
            } else {
                Finish(true);
            }
            break;
        case SDLK_ESCAPE:
            if (e.key.repeat || l.escHeld) break;
            Finish(false);
            break;
        default: break;
        }
        return true;
    }
    case SDL_EVENT_KEY_UP:
        if (e.key.key >= SDLK_F1 && e.key.key <= SDLK_F12) return false;
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) l.enterHeld = false;
        if (e.key.key == SDLK_ESCAPE) l.escHeld = false;
        return true;
    default:
        return false;
    }
}

void Frame(bool top, ImVec2 display) {
    if (!g_live) return;
    g_font = host::Font();
    g_fontKeys = host::KeyFont();
    const ImGuiIO& io = ImGui::GetIO();

    // The mouse (top only): hovering selects (only when the pointer moves), a click presses.
    Live& l = *g_live;
    Geometry g = Compute(l.kb, display);
    const ImVec2 m = io.MousePos;
    const bool mouseValid = top && ImGui::IsMousePosValid(&m);
    const bool moved = mouseValid && l.mouseSeen && (m.x != l.lastMouse.x || m.y != l.lastMouse.y);
    l.lastMouse = m;
    l.mouseSeen = true;
    const bool click = mouseValid && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool closeHover = mouseValid && g.close.Contains(m);
    int answer = -1;  // -1 none, 0 cancel, 1 confirm
    if (mouseValid) {
        for (const KeyBox& kbx : g.boxes) {
            if (!kbx.box.Contains(m)) continue;
            if (moved || click) l.kb.Select(kbx.key->row, kbx.key->col);
            if (click && l.kb.Activate() == Outcome::Done) answer = 1;
            break;
        }
        if (closeHover && click) answer = 0;
    }
    if (answer >= 0) {
        Finish(answer == 1);
        return;
    }

    const uint64_t now = NowMs();
    if (l.kb.editCount() != l.lastEdits) {
        l.lastEdits = l.kb.editCount();
        l.blinkFrom = now;
    }
    g = Compute(l.kb, display);  // the page may have changed
    DrawPanel(l, g, display, closeHover);
}

void Reset() { EndLive(); }

}  // namespace layer

// ---- Test hooks ----

namespace testing {

bool WithKeyboard(const std::function<void(Keyboard&)>& fn) {
    ui::osk::Sync();
    if (!g_live) return false;
    fn(g_live->kb);
    return true;
}

void SetPad(const ps4::PadData* pad) { g_testPad = pad; }

void SetClock(uint64_t (*nowMs)()) { g_testClock = nowMs; }

}  // namespace testing

}  // namespace ui::osk
