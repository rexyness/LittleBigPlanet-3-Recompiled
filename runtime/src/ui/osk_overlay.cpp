// The on-screen keyboard over the game window (see ui/osk_overlay.h), a layer of the overlay
// host (ui/overlay.cpp), which owns ImGui. State that any thread touches (the requested
// keyboard, its id) is under g_mutex; everything else belongs to the presenter thread.
//
// The look follows the PS4 keyboard as shadPS4 measured it (ime_kb_layout.cpp): a 793x528
// panel at 1080p with a title row, the text field, the prediction bar (word suggestions) with a
// close button, and a 10-column key grid; key colours, the selection highlight (a lighter key
// with a white border) and the hotkey badges in each key's top-left corner come from there
// too. Button and key icons are drawn as shapes.
//
// The panel can be moved (the right stick, or the mouse on the title row). Where it is, as a
// fraction of the room it has to move in, carries over to the next keyboard and, through
// user\config\keyboard.ini, to the next run, so a resized window keeps it in the same place.
#include "ui/osk_overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "hle/pad_types.h"
#include "input/gamepad.h"
#include "ui/osk_model.h"
#include "ui/osk_testing.h"
#include "ui/osk_words.h"
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
    std::u16string opening;  // the text it first opened with: only words added to it are learned
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
constexpr float kMoveSpeed = 900.f;  // 1080p pixels a second, the right stick at full tilt (as shadPS4)
constexpr float kMoveDeadzone = 0.2f;
constexpr uint64_t kMaxFrameMs = 50;  // a stall doesn't throw the panel across the screen

// ---- Files (user\config) ----

constexpr const char* kPlacePath = "user\\config\\keyboard.ini";

// LBP3_OSK_PLAIN=1 (the scripted tests): the keyboard as it was before suggestions and the
// remembered place, so runs don't depend on what this PC has learned and leave user\config alone.
bool Plain() {
    static const bool plain = [] {
        const char* v = std::getenv("LBP3_OSK_PLAIN");
        return v && *v && std::strcmp(v, "0") != 0;
    }();
    return plain;
}

bool g_useFiles = true;  // osktest turns it off
bool FilesOn() { return g_useFiles && !Plain(); }

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    std::string text = s.str();
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);  // a BOM from a hand edit
    return text;
}

// Through a temporary file and a rename, so a crash mid-write never leaves half a file.
bool WriteFile(const char* path, const std::string& text) {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out.flush()) return false;
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::fprintf(stderr, "[osk] couldn't write %s: %s\n", path, ec.message().c_str());
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

// Each line of a small hand-editable file, without CR or surrounding blanks.
template <typename Fn>
void ForEachLine(std::string_view text, Fn&& fn) {
    size_t at = 0;
    while (at <= text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view line = text.substr(at, nl - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        fn(line);
        at = nl + 1;
    }
}

// ---- Where the panel is ----

// 0 = against the left (top) edge, 1 = against the right (bottom), 0.5 = centred.
struct Place {
    float x = 0.5f, y = 0.5f;
};
Place g_place;
bool g_placeLoaded = false, g_placeDirty = false;

void LoadPlace() {
    if (g_placeLoaded) return;
    g_placeLoaded = true;
    if (!FilesOn()) return;
    const auto text = ReadFile(kPlacePath);
    if (!text) return;
    Place p;
    ForEachLine(*text, [&](std::string_view line) {
        if (line.size() < 3 || line[1] != '=' || (line[0] != 'x' && line[0] != 'y')) return;
        const std::string value(line.substr(2));
        char* end = nullptr;
        const float v = std::strtof(value.c_str(), &end);
        if (end == value.c_str() || !std::isfinite(v)) return;
        (line[0] == 'x' ? p.x : p.y) = std::clamp(v, 0.f, 1.f);
    });
    g_place = p;
    std::fprintf(stderr, "[osk] keyboard place %.3f,%.3f (%s)\n", p.x, p.y, kPlacePath);
}

void SavePlace() {
    if (!g_placeDirty || !FilesOn()) return;
    g_placeDirty = false;
    char text[256];
    std::snprintf(text, sizeof(text),
                  "; Where the on-screen keyboard sits: 0 = against the left (top) edge, 1 = against the right\n"
                  "; (bottom), 0.5 = centred. Delete this file to centre it again.\n"
                  "x=%.4f\ny=%.4f\n",
                  g_place.x, g_place.y);
    WriteFile(kPlacePath, text);
}

// ---- Suggestions ----

constexpr const char* kLearnedPath = "user\\config\\learned_words.txt";
constexpr const char* kListFile = "data\\osk\\words_en.txt";

Words g_words;
bool g_listLoaded = false, g_learnedLoaded = false;
std::optional<std::string> g_testList;  // osktest's list, instead of data\osk

// LBP3_OSK_SUGGEST=off: the launcher's "Word suggestions" switch turned off. No suggestions,
// nothing learned.
bool SuggestOff() {
    static const bool off = [] {
        const char* v = std::getenv("LBP3_OSK_SUGGEST");
        return v && (_stricmp(v, "off") == 0 || std::strcmp(v, "0") == 0);
    }();
    return off;
}

bool SuggestionsOn() { return g_testList.has_value() || (FilesOn() && !SuggestOff()); }

// The word list ships beside lbp3.exe (data\osk); in a build folder it's one level up, in the
// checkout. The game folder (the working folder) is the last place looked.
void LoadWords() {
    if (!g_listLoaded) {
        g_listLoaded = true;
        if (g_testList) {
            g_words.LoadList(*g_testList);
        } else {
            std::vector<std::filesystem::path> places;
            if (const char* base = SDL_GetBasePath()) {
                const std::filesystem::path exe = std::filesystem::u8path(base);
                places.push_back(exe / kListFile);
                places.push_back(exe / ".." / kListFile);
            }
            places.push_back(kListFile);
            const uint64_t t0 = SDL_GetTicks();
            bool found = false;
            for (const auto& p : places) {
                const auto text = ReadFile(p);
                if (!text) continue;
                g_words.LoadList(*text);
                std::fprintf(stderr, "[osk] word list: %zu words from %s (%llu ms)\n", g_words.listSize(),
                             p.lexically_normal().string().c_str(), static_cast<unsigned long long>(SDL_GetTicks() - t0));
                found = true;
                break;
            }
            if (!found) std::fprintf(stderr, "[osk] no word list (%s): suggestions come from learned words only\n", kListFile);
        }
    }
    if (!g_learnedLoaded) {
        g_learnedLoaded = true;
        if (FilesOn()) {
            if (const auto text = ReadFile(kLearnedPath)) g_words.LoadLearned(*text);
            std::fprintf(stderr, "[osk] %zu learned words (%s)\n", g_words.learnedSize(), kLearnedPath);
        }
    }
}

void LogMode() {
    static bool logged = false;
    if (logged) return;
    logged = true;
    if (Plain()) {
        std::fprintf(stderr, "[osk] plain keyboard (LBP3_OSK_PLAIN): no suggestions, nothing learned, place not kept\n");
    } else if (SuggestOff()) {
        std::fprintf(stderr, "[osk] word suggestions off (LBP3_OSK_SUGGEST=off)\n");
    }
}

// The prediction bar's layout, in 1080p units: the suggestions side by side from the left, the
// first with the R3 badge in front of it.
constexpr float kBarText = 26.f, kBarPad = 18.f, kBarMinSlot = 70.f, kBarMargin = 8.f, kBarBadgeGap = 8.f;
constexpr float kBarWidth = 740.f;
float g_barScale = 1.f;  // the last frame's scale: the bar is fitted for it
float HotkeyWidth(const char* hotkey, float s);

// Each suggestion's slot, in pixels at scale s.
std::vector<float> SlotWidths(const std::vector<std::u16string>& words, float s) {
    std::vector<float> out;
    for (size_t i = 0; i < words.size(); ++i) {
        const std::string u8 = Utf8(words[i]);
        float w = g_font ? g_font->CalcTextSizeA(kBarText * s, 1e9f, 0.f, u8.c_str()).x : 0.f;
        w = std::max(kBarMinSlot * s, w + kBarPad * 2.f * s);
        if (i == 0) w += HotkeyWidth("R3", s) + kBarBadgeGap * s;
        out.push_back(w);
    }
    return out;
}

// As many suggestions as fit in the bar (all of them before the fonts exist).
void FitBar(std::vector<std::u16string>& words) {
    if (!g_font) return;
    const float s = g_barScale;
    const std::vector<float> widths = SlotWidths(words, s);
    float x = kBarMargin * s;
    size_t n = 0;
    while (n < widths.size() && x + widths[n] <= (kBarWidth - kBarMargin) * s) x += widths[n++];
    words.resize(n);
}

struct Live {
    explicit Live(Pending p)
        : id(p.id), stamp(p.stamp), request(std::move(p.request)), onAnswer(std::move(p.onAnswer)),
          opening(std::move(p.opening)), kb(request.text, request.maxLength, request.multiline) {}

    uint64_t id, stamp;
    bool wasTop = true;  // false while a newer overlay (a system dialog) has the input
    Request request;
    AnswerFn onAnswer;
    std::u16string opening;
    bool learn = false;  // suggestions are on for this one: confirming it teaches words
    Keyboard kb;
    // Pad: buttons held when the keyboard came up count only after a release.
    uint32_t ignore = 0, prev = 0;
    bool stickIgnored = false, rightIgnored = false;
    // The right stick this frame, deadzone applied (-1..1); Frame moves the panel by it.
    float moveX = 0.f, moveY = 0.f;
    uint64_t lastFrameMs = 0;
    // The mouse holding the title row: the panel follows it.
    bool dragging = false;
    ImVec2 grab{};
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
    SavePlace();
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
    l.rightIgnored = ok && (std::abs(int(pad.rightStick.x) - 128) > kStickCentre ||
                            std::abs(int(pad.rightStick.y) - 128) > kStickCentre);
    l.moveX = l.moveY = 0.f;
    l.dragging = false;
    for (Repeater* r : {&l.up, &l.down, &l.left, &l.right, &l.cross, &l.square, &l.triangle, &l.l1, &l.r1}) r->Reset();
    l.l2Down = l.l2Combo = l.triangleCombo = false;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    l.enterHeld = keys && (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]);
    l.escHeld = keys && keys[SDL_SCANCODE_ESCAPE];
}

void StartLive(Pending p) {
    host::EnsureImGui();
    g_font = host::Font();  // the bar measures its suggestions with it
    g_fontKeys = host::KeyFont();
    LogMode();
    LoadPlace();
    g_live.emplace(std::move(p));
    Live& l = *g_live;
    if (SuggestionsOn() && !l.request.password) {
        LoadWords();
        l.learn = true;
        l.kb.SetSuggester([](std::u16string_view typed, size_t room) {
            std::vector<std::u16string> words = g_words.Suggest(typed, room);
            FitBar(words);
            return words;
        });
    }
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
    // Confirmed text teaches the words added to what the keyboard opened with.
    if (confirmed && g_live->learn && g_words.Learn(g_live->opening, answer.text) && FilesOn())
        WriteFile(kLearnedPath, g_words.SaveLearned());
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
    // The right stick moves the panel, faster the further it's pushed; Frame applies it.
    l.moveX = l.moveY = 0.f;
    if (ok) {
        const int rx = int(pad.rightStick.x) - 128, ry = int(pad.rightStick.y) - 128;
        if (l.rightIgnored && std::abs(rx) <= kStickCentre && std::abs(ry) <= kStickCentre) l.rightIgnored = false;
        const float fx = std::clamp(rx / 127.f, -1.f, 1.f), fy = std::clamp(ry / 127.f, -1.f, 1.f);
        const float m = std::hypot(fx, fy);
        if (!l.rightIgnored && m > kMoveDeadzone) {
            const float k = std::min(1.f, (m - kMoveDeadzone) / (1.f - kMoveDeadzone)) / m;
            l.moveX = fx * k;
            l.moveY = fy * k;
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
        const bool repeats = !kb.inBar() && (a == Action::Backspace || a == Action::Space || a == Action::ArrowLeft ||
                                             a == Action::ArrowRight);
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
    if (pressed(ps4::PAD_R3)) kb.Pick(0);  // the first suggestion
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
    std::vector<Box> slots;     // the suggestions in the prediction bar
};
Geometry g_lastGeometry;  // for the test hooks

float Scale(ImVec2 display) { return std::max(0.1f, std::min(display.x / 1920.f, display.y / 1080.f)); }

ImVec2 PanelSize(const Keyboard& kb, float s) { return {793.f * s, (kb.multiline() ? 628.f : 528.f) * s}; }

// The room the panel has to move in, in pixels (0 where it fills the window).
ImVec2 FreeRoom(const Keyboard& kb, ImVec2 display) {
    const ImVec2 size = PanelSize(kb, Scale(display));
    return {std::max(0.f, display.x - size.x), std::max(0.f, display.y - size.y)};
}

// Puts the panel's top-left corner at `corner` (pixels), kept inside the window.
void PlaceAt(const Keyboard& kb, ImVec2 display, ImVec2 corner) {
    const ImVec2 room = FreeRoom(kb, display);
    const Place before = g_place;
    if (room.x > 0.f) g_place.x = std::clamp(corner.x / room.x, 0.f, 1.f);
    if (room.y > 0.f) g_place.y = std::clamp(corner.y / room.y, 0.f, 1.f);
    if (g_place.x != before.x || g_place.y != before.y) g_placeDirty = true;
}

ImVec2 Corner(const Keyboard& kb, ImVec2 display) {
    const ImVec2 room = FreeRoom(kb, display);
    return {room.x * g_place.x, room.y * g_place.y};
}

Geometry Compute(const Keyboard& kb, ImVec2 display) {
    Geometry g;
    const float s = Scale(display);
    g.s = s;
    const bool multi = kb.multiline();
    const ImVec2 size = PanelSize(kb, s);
    const float panelW = size.x, panelH = size.y;
    const ImVec2 corner = Corner(kb, display);
    const float x = std::floor(corner.x), y = std::floor(corner.y);
    g.panel = {{x, y}, {x + panelW, y + panelH}};
    const float labelH = 57.f * s, inputH = (multi ? 151.f : 50.f) * s, predictH = 53.f * s, closeW = 53.f * s;
    const float keysH = 316.f * s, padX = 26.f * s, padBottom = 26.f * s;
    const float gapV = std::max(0.f, (panelH - (labelH + inputH + predictH + keysH + padBottom)) * 0.5f);
    g.title = {{x + padX, y}, {x + panelW - padX, y + labelH}};
    g.input = {{x + padX, y + labelH}, {x + panelW - padX, y + labelH + inputH}};
    const float py = y + labelH + inputH + gapV;
    g.predict = {{x, py}, {x + 740.f * s, py + predictH}};
    g.close = {{x + panelW - closeW, py}, {x + panelW, py + predictH}};
    float sx = g.predict.min.x + kBarMargin * s;
    for (float w : SlotWidths(kb.suggestions(), s)) {
        g.slots.push_back({{std::round(sx), g.predict.min.y}, {std::round(sx + w), g.predict.max.y}});
        sx += w;
    }
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
struct HotkeyPart {
    std::string text;
    bool icon = false;
    Icon which = Icon::Triangle;
};

// Words are text, TRI/SQR are icons, joined by '+'.
std::vector<HotkeyPart> HotkeyParts(const char* hotkey) {
    std::vector<HotkeyPart> parts;
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
    return parts;
}

struct HotkeyMetrics {
    float size, iconW, textW, padX, padY;
    ImVec2 Badge() const { return {textW + padX * 2.f, size + padY * 2.f}; }
};

HotkeyMetrics MeasureHotkey(const std::vector<HotkeyPart>& parts, float s) {
    HotkeyMetrics m{};
    m.size = std::max(8.f, 16.f * s);
    m.iconW = m.size * 0.78f;
    for (const HotkeyPart& p : parts) m.textW += p.icon ? m.iconW : (g_font ? TextSize(g_font, m.size, p.text.c_str()).x : 0.f);
    m.padX = std::max(2.f, m.size * 0.3f);
    m.padY = std::max(1.f, m.size * 0.08f);
    return m;
}

float HotkeyWidth(const char* hotkey, float s) { return MeasureHotkey(HotkeyParts(hotkey), s).Badge().x; }
float HotkeyHeight(const char* hotkey, float s) { return MeasureHotkey(HotkeyParts(hotkey), s).Badge().y; }

// A hotkey hint ("L2", "R2", "TRI", "L2+TRI"...) in a dark badge with its top-left at bmin.
void DrawHotkeyAt(ImDrawList* dl, ImVec2 bmin, const char* hotkey, float s) {
    const std::vector<HotkeyPart> parts = HotkeyParts(hotkey);
    const HotkeyMetrics hm = MeasureHotkey(parts, s);
    const float size = hm.size, iconW = hm.iconW, padX = hm.padX;
    const ImVec2 bmax{bmin.x + hm.Badge().x, bmin.y + hm.Badge().y};
    const float round = std::max(2.f, size * 0.24f);
    dl->AddRectFilled(bmin, bmax, IM_COL32(10, 10, 10, 140), round);
    dl->AddRect(bmin, bmax, IM_COL32(92, 92, 92, 110), round, 1.f);
    float x = bmin.x + padX;
    const float cy = (bmin.y + bmax.y) * 0.5f;
    for (const HotkeyPart& p : parts) {
        if (p.icon) {
            DrawIcon(dl, {x + iconW * 0.5f, cy}, iconW * 0.92f, p.which, kHotkeyText, std::max(1.f, 1.4f * s));
            x += iconW;
        } else {
            dl->AddText(g_font, size, {x, cy - size * 0.5f - size * 0.06f}, kHotkeyText, p.text.c_str());
            x += TextSize(g_font, size, p.text.c_str()).x;
        }
    }
}

// The badge in a key's top-left corner.
void DrawHotkey(ImDrawList* dl, const Box& key, const char* hotkey, float s) {
    if (!hotkey || !*hotkey) return;
    DrawHotkeyAt(dl, {key.min.x + std::max(3.f, 4.f * s), key.min.y + std::max(2.f, 3.f * s)}, hotkey, s);
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

    // The prediction bar and the close button. The suggestions sit side by side, the selected
    // one lit like a key, the first with the R3 badge in front of it.
    dl->AddRectFilled(g.predict.min, g.predict.max, kPreedit);
    dl->AddLine({g.panel.min.x, g.predict.min.y}, {g.panel.max.x, g.predict.min.y}, kLine);
    dl->AddLine({g.panel.min.x, g.predict.max.y}, {g.panel.max.x, g.predict.max.y}, kLine);
    const auto& words = kb.suggestions();
    for (size_t i = 0; i < g.slots.size() && i < words.size(); ++i) {
        const Box& slot = g.slots[i];
        const bool sel = kb.barIndex() == int(i);
        if (sel) {
            dl->AddRectFilled(slot.min, slot.max, Lighter(kPreedit, 0.11f));
            dl->AddRect(slot.min, slot.max, kSelectedBorder, 0.f, std::max(2.f, 3.f * s));
        } else if (i + 1 < g.slots.size()) {
            dl->AddLine({slot.max.x, slot.min.y + 12.f * s}, {slot.max.x, slot.max.y - 12.f * s}, kLine);
        }
        float tx = slot.min.x + kBarPad * s;
        if (i == 0) {
            const float bh = HotkeyHeight("R3", s);
            DrawHotkeyAt(dl, {tx, std::round(slot.Centre().y - bh * 0.5f)}, "R3", s);
            tx += HotkeyWidth("R3", s) + kBarBadgeGap * s;
        }
        const std::string u8 = Utf8(words[i]);
        const float size = kBarText * s;
        const ImVec2 ts = TextSize(g_font, size, u8.c_str());
        dl->AddText(g_font, size, {tx, slot.min.y + (slot.Size().y - ts.y) * 0.5f}, kText, u8.c_str());
    }
    dl->AddRectFilled(g.close.min, g.close.max, closeHover ? Lighter(kKeyFunction, 0.11f) : kKeyFunction);
    dl->AddLine(g.close.min, {g.close.min.x, g.close.max.y}, kLine);
    DrawIcon(dl, g.close.Centre(), g.close.Size().y * 0.46f, Icon::Circle, kText, std::max(1.5f, 2.2f * s));

    const KeySpec* sel = kb.inBar() ? nullptr : kb.selected();
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
    std::u16string opening = request.text;
    g_pending = Pending{id, host::NextStamp(), std::move(request), std::move(onAnswer), std::move(opening)};
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
        l.moveX = l.moveY = 0.f;
        l.dragging = false;
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
        case SDLK_TAB: kb.Pick(0); break;  // the first suggestion
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
    g_barScale = Scale(display);
    const ImGuiIO& io = ImGui::GetIO();

    Live& l = *g_live;
    // The right stick moves the panel by the time since the last frame.
    const uint64_t frameMs = NowMs();
    const float dt = l.lastFrameMs ? float(std::min(frameMs - l.lastFrameMs, kMaxFrameMs)) / 1000.f : 0.f;
    l.lastFrameMs = frameMs;
    if (top && dt > 0.f && (l.moveX != 0.f || l.moveY != 0.f)) {
        const float step = kMoveSpeed * Scale(display) * dt;
        const ImVec2 c = Corner(l.kb, display);
        PlaceAt(l.kb, display, {c.x + l.moveX * step, c.y + l.moveY * step});
    }

    // The mouse (top only): hovering selects (only when the pointer moves), a click presses;
    // the title row drags the panel.
    Geometry g = Compute(l.kb, display);
    const ImVec2 m = io.MousePos;
    const bool mouseValid = top && ImGui::IsMousePosValid(&m);
    const bool moved = mouseValid && l.mouseSeen && (m.x != l.lastMouse.x || m.y != l.lastMouse.y);
    l.lastMouse = m;
    l.mouseSeen = true;
    const bool click = mouseValid && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const Box titleRow{g.panel.min, {g.panel.max.x, g.input.min.y}};
    if (click && titleRow.Contains(m)) {
        l.dragging = true;
        l.grab = {m.x - g.panel.min.x, m.y - g.panel.min.y};
    }
    if (l.dragging) {
        if (mouseValid && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            PlaceAt(l.kb, display, {m.x - l.grab.x, m.y - l.grab.y});
            g = Compute(l.kb, display);
        } else {
            l.dragging = false;
        }
    }
    const bool closeHover = mouseValid && g.close.Contains(m);
    int answer = -1;  // -1 none, 0 cancel, 1 confirm
    if (mouseValid) {
        for (size_t i = 0; i < g.slots.size(); ++i) {  // a suggestion: hover selects it, a click picks it
            if (!g.slots[i].Contains(m)) continue;
            if (moved || click) l.kb.SelectSuggestion(i);
            if (click) l.kb.Pick(i);
            break;
        }
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
    g_lastGeometry = g;
    DrawPanel(l, g, display, closeHover);
}

void Reset() {
    // A new window: the keyboard that is up goes back to pending with what has been typed so
    // far, so it comes up again on it and still gets answered.
    if (!g_live) return;
    Request r = g_live->request;
    r.text = g_live->kb.text();
    Pending p{g_live->id, g_live->stamp, std::move(r), std::move(g_live->onAnswer), g_live->opening};
    EndLive();
    std::lock_guard lock(g_mutex);
    if (!g_pending && g_current.load() == p.id) g_pending = std::move(p);
}

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

void UseFiles(bool on) { g_useFiles = on; }

void ForgetFiles() {
    g_placeLoaded = false;
    g_placeDirty = false;
    g_place = Place{};
    g_words = Words{};
    g_listLoaded = g_learnedLoaded = false;
}

void SetWordList(const char* utf8) {
    if (utf8) {
        g_testList = utf8;
    } else {
        g_testList.reset();
    }
    g_words = Words{};
    g_listLoaded = g_learnedLoaded = false;
}

uint32_t LearnedCount(std::u16string_view word) { return g_words.LearnedCount(word); }

bool SuggestionCentre(size_t index, float& x, float& y) {
    if (index >= g_lastGeometry.slots.size()) return false;
    const ImVec2 c = g_lastGeometry.slots[index].Centre();
    x = c.x;
    y = c.y;
    return true;
}

void GetPlace(float& x, float& y) {
    x = g_place.x;
    y = g_place.y;
}

void SetPlace(float x, float y) {
    g_place = {std::clamp(x, 0.f, 1.f), std::clamp(y, 0.f, 1.f)};
    g_placeDirty = true;
}

}  // namespace testing

}  // namespace ui::osk
