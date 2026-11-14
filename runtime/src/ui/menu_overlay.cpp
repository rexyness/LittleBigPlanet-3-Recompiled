// The in-game menu: a layer of the overlay host. See menu_overlay.h.
#include "ui/menu_overlay.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "hle/audio_downmix.h"
#include "hle/modal.h"
#include "hle/my_unlocks.h"
#include "hle/pad_types.h"
#include "hle/quit.h"
#include "input/gamepad.h"
#include "ui/menu_model.h"
#include "ui/overlay_layers.h"
#include "video/framegen.h"
#include "video/live_stats.h"

namespace ui::menu {
namespace {

namespace host = ui::overlay::detail;

constexpr ImU32 kPanel = IM_COL32(18, 22, 34, 236);
constexpr ImU32 kEdge = IM_COL32(90, 120, 200, 200);
constexpr ImU32 kRowSel = IM_COL32(52, 96, 196, 235);
constexpr ImU32 kText = IM_COL32(232, 236, 245, 255);
constexpr ImU32 kDimText = IM_COL32(150, 158, 175, 255);
constexpr ImU32 kWarn = IM_COL32(255, 196, 92, 255);

// Presenter thread.
bool g_open = false;
uint64_t g_stamp = 0;
Model g_model;
bool g_modelInit = false;
uint32_t g_prevButtons = 0;  // menu navigation edges
uint32_t g_ignore = 0;       // buttons held when it opened, until released
uint32_t g_hotPrev = 0;      // L3+R3 edge, polled even while closed
bool g_hud = false;
bool g_hudInit = false;

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

Unlocks UnlocksState() {
    static_assert(static_cast<int>(hle::my_unlocks::State::Done) == static_cast<int>(Unlocks::Done));
    return static_cast<Unlocks>(hle::my_unlocks::GetState());
}

void InitModel() {
    if (!g_modelInit) {
        g_modelInit = true;
        g_model.frameGenAvailable = video::framegen::Enabled();
    }
    g_model.frameGenOn = g_model.frameGenAvailable && !video::framegen::Paused();
    g_model.volume = static_cast<int>(std::lround(hle::audio::MasterVolumeLevel() * 100.0f));
    g_model.stats = g_hud;
    g_model.unlocks = UnlocksState();
}

bool HudFromEnv() {
    if (!g_hudInit) {
        g_hudInit = true;
        const char* v = std::getenv("LBP3_STATS");
        g_hud = v && *v && std::strcmp(v, "0") != 0;
    }
    return g_hud;
}

void Apply(Action a) {
    switch (a) {
    case Action::ToggleStats:
        g_hud = g_model.stats;
        std::fprintf(stderr, "[menu] stats overlay %s\n", g_hud ? "on" : "off");
        break;
    case Action::ToggleFrameGen:
        video::framegen::SetPaused(!g_model.frameGenOn);
        break;
    case Action::Volume:
        hle::audio::SetMasterVolumeLevel(g_model.volume / 100.0f);
        std::fprintf(stderr, "[menu] volume %d%%\n", g_model.volume);
        break;
    case Action::ArmUnlocks:
        if (!hle::my_unlocks::Request()) g_model.unlocks = UnlocksState();
        break;
    case Action::CancelUnlocks: hle::my_unlocks::Cancel(); break;
    case Action::Quit:
        hle::quit::RequestQuit("the in-game menu's Quit game");
        break;
    case Action::Close: Toggle(); break;
    default: break;
    }
}

void OpenMenu() {
    InitModel();
    g_model.selected = 0;
    g_model.quitArmedUntil = 0;
    host::EnsureImGui();
    g_open = true;
    g_stamp = host::NextStamp();
    ps4::PadData pad{};
    g_ignore = input::Read(0, pad, false) ? pad.buttons : 0;  // the hotkey itself, until released
    g_prevButtons = g_ignore;
    hle::modal::Begin("menu");
    std::fprintf(stderr, "[menu] opened\n");
}

void CloseMenu() {
    g_open = false;
    g_stamp = 0;
    host::ReleaseMouse();
    hle::modal::End();
    std::fprintf(stderr, "[menu] closed\n");
}

std::string Fmt(const char* f, double a) {
    char b[64];
    std::snprintf(b, sizeof(b), f, a);
    return b;
}

// One line of numbers for the corner overlay and the menu's header.
std::string StatsLine(const video::live::Snapshot& s) {
    if (!s.valid) return "measuring...";
    std::string line = Fmt("%.0f fps", s.shownFps);
    if (s.generatedShare > 0.01) line += Fmt(" (game %.0f)", s.gameFps);
    line += Fmt("  |  game work %.1f ms", s.workMs);
    if (s.latencyMs >= 0) line += Fmt("  |  latency %.0f ms", s.latencyMs);
    return line;
}

// The footer while "Import LBP1/LBP2 items" is selected.
const char* UnlocksHelp(Unlocks u) {
    switch (u) {
    case Unlocks::Idle:
        return "Adds the LBP1 and LBP2 items that come with LBP3 to your profile (the old My Unlocks, whose servers "
               "are gone). Cross: arm it.";
    case Unlocks::Armed:
        return "Armed: open the start menu (Options) and it starts. It takes about 2 minutes; then press Cross on "
               "\"successfully imported\". Cross here: cancel.";
    case Unlocks::Running:
        return "Importing: wait about 2 minutes, then press Cross on \"successfully imported\". Circle can't cancel "
               "it.";
    case Unlocks::Done:
        return "Finished: the game's message said how it went. Cross runs it again (on a profile that has the items, "
               "that adds nothing).";
    default: return "Not available in this session (LBP3_MY_UNLOCKS=off).";
    }
}

struct Row {
    ImVec2 min, max;
};

void DrawMenu(ImVec2 display, bool top) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = std::max(0.5f, display.y / 1080.0f);
    ImFont* font = host::Font();
    ImFont* bold = host::KeyFont();
    const float w = 640.f * s, rowH = 54.f * s, pad = 28.f * s;
    const float titleSize = 40.f * s, textSize = 28.f * s, smallSize = 22.f * s;
    const int n = static_cast<int>(Item::kCount);
    const float h = pad * 2 + titleSize + 12.f * s + smallSize * 3.6f + 18.f * s + rowH * n + 10.f * s + smallSize * 3.6f;
    const ImVec2 p0{(display.x - w) * 0.5f, (display.y - h) * 0.5f}, p1{p0.x + w, p0.y + h};
    dl->AddRectFilled(p0, p1, kPanel, 18.f * s);
    dl->AddRect(p0, p1, kEdge, 18.f * s, 2.f * s);
    float y = p0.y + pad;
    dl->AddText(bold, titleSize, {p0.x + pad, y}, kText, "Game menu");
    y += titleSize + 12.f * s;

    const video::live::Snapshot st = video::live::Read();
    dl->AddText(font, smallSize, {p0.x + pad, y}, kDimText, StatsLine(st).c_str());
    y += smallSize * 1.2f;
    std::string l2 = st.valid ? Fmt("frame %.1f ms avg", st.frameMs) + Fmt(", %.1f ms worst", st.worstFrameMs) : "";
    if (video::framegen::Enabled())
        l2 += st.valid ? Fmt("  |  generated %.0f%%", st.generatedShare * 100.0) : "";
    dl->AddText(font, smallSize, {p0.x + pad, y}, kDimText, l2.c_str());
    y += smallSize * 1.2f;
    const std::string l3 = video::framegen::Enabled()
                               ? (video::framegen::Paused() ? "Frame generation: paused" : "Frame generation: on")
                               : "Frame generation: off (set FRAMEGEN=1 in play.bat)";
    dl->AddText(font, smallSize, {p0.x + pad, y}, kDimText, l3.c_str());
    y += smallSize * 1.2f + 18.f * s;

    const uint64_t now = NowMs();
    g_model.unlocks = UnlocksState();  // it moves on by itself (armed -> importing -> done)
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (int i = 0; i < n; ++i) {
        const Item item = static_cast<Item>(i);
        const Row r{{p0.x + pad * 0.5f, y}, {p1.x - pad * 0.5f, y + rowH - 6.f * s}};
        const bool enabled = g_model.Enabled(item);
        if (top && enabled && mouse.x >= r.min.x && mouse.x < r.max.x && mouse.y >= r.min.y && mouse.y < r.max.y) {
            g_model.selected = i;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) Apply(g_model.Activate(now));
        }
        const bool sel = g_model.selected == i;
        if (sel) dl->AddRectFilled(r.min, r.max, kRowSel, 10.f * s);
        std::string label, value;
        switch (item) {
        case Item::Stats: label = "Stats overlay"; value = g_model.stats ? "On" : "Off"; break;
        case Item::FrameGen:
            label = "Frame generation";
            value = g_model.frameGenAvailable ? (g_model.frameGenOn ? "On" : "Off") : "not started";
            break;
        case Item::Volume: label = "Volume"; value = std::to_string(g_model.volume) + "%"; break;
        case Item::Unlocks:
            label = "Import LBP1/LBP2 items";
            switch (g_model.unlocks) {
            case Unlocks::Unavailable: value = "off"; break;
            case Unlocks::Armed: value = "armed"; break;
            case Unlocks::Running: value = "importing..."; break;
            case Unlocks::Done: value = "done"; break;
            default: break;
            }
            break;
        case Item::Quit: label = g_model.QuitArmed(now) ? "Press again to quit" : "Quit game"; break;
        case Item::Close: label = "Back to the game"; break;
        default: break;
        }
        const bool warn = (item == Item::Quit && g_model.QuitArmed(now)) ||
                          (item == Item::Unlocks && g_model.unlocks == Unlocks::Armed);
        const ImU32 col = !enabled && !(item == Item::Unlocks && g_model.unlocks == Unlocks::Running) ? kDimText
                          : warn                                                                    ? kWarn
                                                                                                    : kText;
        const float ty = r.min.y + (r.max.y - r.min.y - textSize) * 0.5f;
        dl->AddText(font, textSize, {r.min.x + 18.f * s, ty}, col, label.c_str());
        if (!value.empty()) {
            const std::string shown = item == Item::Volume ? "< " + value + " >" : value;
            const ImVec2 vs = font->CalcTextSizeA(textSize, FLT_MAX, 0.f, shown.c_str());
            dl->AddText(font, textSize, {r.max.x - 18.f * s - vs.x, ty}, col, shown.c_str());
        }
        y += rowH;
    }
    y += 10.f * s;
    const char* foot = "Cross / Enter: choose    Left / Right: change    Circle / Esc / L3+R3 / Insert: back";
    if (g_model.Selected() == Item::Unlocks) foot = UnlocksHelp(g_model.unlocks);
    dl->AddText(font, smallSize, {p0.x + pad, y}, kDimText, foot, nullptr, w - pad * 2);
}

void DrawHud(ImVec2 display) {
    const float s = std::max(0.5f, display.y / 1080.0f);
    const std::string line = StatsLine(video::live::Read());
    ImFont* font = host::Font();
    const float size = 22.f * s;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, line.c_str());
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 p0{12.f * s, 12.f * s}, p1{p0.x + ts.x + 20.f * s, p0.y + ts.y + 10.f * s};
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 150), 8.f * s);
    dl->AddText(font, size, {p0.x + 10.f * s, p0.y + 5.f * s}, kText, line.c_str());
}

}  // namespace

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_MENU");
        return !(v && (std::strcmp(v, "off") == 0 || std::strcmp(v, "0") == 0));
    }();
    return on;
}

void Toggle() {
    if (!Enabled()) return;
    if (g_open) {
        CloseMenu();
    } else {
        OpenMenu();
    }
}

bool IsOpen() { return g_open; }

namespace layer {

bool Wanted() { return g_open; }

uint64_t Sync() { return g_open ? g_stamp : 0; }

bool Poll(bool top) {
    if (!g_open) return false;
    ps4::PadData pad{};
    const uint32_t raw = input::Read(0, pad, false) ? pad.buttons : 0;
    g_ignore &= raw;
    const uint32_t b = raw & ~g_ignore;
    const auto pressed = [&](uint32_t mask) { return (b & mask) && !(g_prevButtons & mask); };
    if (top) {
        const uint64_t now = NowMs();
        if (pressed(ps4::PAD_UP)) g_model.Move(-1);
        if (pressed(ps4::PAD_DOWN)) g_model.Move(1);
        if (pressed(ps4::PAD_LEFT)) Apply(g_model.Adjust(-1));
        if (pressed(ps4::PAD_RIGHT)) Apply(g_model.Adjust(1));
        if (pressed(ps4::PAD_CROSS)) Apply(g_model.Activate(now));
        if (g_open && (pressed(ps4::PAD_CIRCLE) || pressed(ps4::PAD_OPTIONS))) CloseMenu();
    }
    g_prevButtons = b;
    return g_open;
}

bool Event(const SDL_Event& e) {
    if (!g_open) return false;
    switch (e.type) {
    case SDL_EVENT_TEXT_INPUT: return true;
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode key = e.key.key;
        if (key >= SDLK_F1 && key <= SDLK_F12) return false;  // the debug hotkeys keep working
        if (e.key.repeat && key != SDLK_UP && key != SDLK_DOWN && key != SDLK_LEFT && key != SDLK_RIGHT) return true;
        const uint64_t now = NowMs();
        switch (key) {
        case SDLK_UP: g_model.Move(-1); break;
        case SDLK_DOWN: g_model.Move(1); break;
        case SDLK_LEFT: Apply(g_model.Adjust(-1)); break;
        case SDLK_RIGHT: Apply(g_model.Adjust(1)); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE: Apply(g_model.Activate(now)); break;
        case SDLK_ESCAPE:
        case SDLK_INSERT: CloseMenu(); break;
        default: break;
        }
        return true;
    }
    case SDL_EVENT_KEY_UP: return !(e.key.key >= SDLK_F1 && e.key.key <= SDLK_F12);
    default: return false;
    }
}

void Frame(bool top, ImVec2 display) {
    if (g_open) DrawMenu(display, top);
}

void Reset() {
    if (g_open) CloseMenu();
}

void PollHotkey(bool othersUp) {
    if (!Enabled()) return;
    ps4::PadData pad{};
    const uint32_t raw = input::Read(0, pad, false) ? pad.buttons : 0;
    constexpr uint32_t kHot = ps4::PAD_L3 | ps4::PAD_R3;
    const bool now = (raw & kHot) == kHot, before = (g_hotPrev & kHot) == kHot;
    g_hotPrev = raw;
    if (now && !before && (g_open || !othersUp)) Toggle();
}

bool HotkeyEvent(const SDL_Event& e, bool othersUp) {
    if (!Enabled() || e.type != SDL_EVENT_KEY_DOWN || e.key.repeat || e.key.key != SDLK_INSERT) return false;
    if (g_open) return false;  // Event closes it
    if (othersUp) return false;
    Toggle();
    return true;
}

bool Hud() { return HudFromEnv(); }

void DrawHudLayer(ImVec2 display) {
    if (!g_open && Hud()) DrawHud(display);
}

}  // namespace layer

}  // namespace ui::menu
