#include "input/actions.h"
#include "input/game_state.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace input::actions {
namespace {

constexpr const char* kConfigPath = "user\\config\\input.ini";

struct ActionInfo {
    Action action;
    const char* name;        // key in input.ini
    const char* defaults;    // comma-separated SDL key names
    const char* description;
};

constexpr ActionInfo kActions[] = {
    {Action::MoveLeft, "move_left", "A", "walk left"},
    {Action::MoveRight, "move_right", "D", "walk right"},
    {Action::LayerBack, "layer_back", "W", "step one layer back (away from the camera)"},
    {Action::LayerFront, "layer_front", "S", "step one layer forward (towards the camera)"},
    {Action::Jump, "jump", "Space", "jump (hold to jump higher)"},
    {Action::Grab, "grab", "Left Shift", "grab and hold on"},
    {Action::OpenComputer, "open_computer", "E", "use the Pod computer"},
    {Action::OpenPopit, "open_popit", "R", "open the Popit"},
    {Action::MenuUp, "menu_up", "Up", "menus: move up"},
    {Action::MenuDown, "menu_down", "Down", "menus: move down"},
    {Action::MenuLeft, "menu_left", "Left", "menus: move left"},
    {Action::MenuRight, "menu_right", "Right", "menus: move right"},
    {Action::Confirm, "confirm", "Return", "menus: select"},
    {Action::Back, "back", "Escape, Backspace", "menus: back / close"},
    {Action::Pause, "pause", "Tab", "pause menu"},
    {Action::TiltLeft, "tilt_left", "Keypad 4", "tilt the pad left (for pads without motion sensors, and the keyboard)"},
    {Action::TiltRight, "tilt_right", "Keypad 6", "tilt the pad right"},
    {Action::TiltForward, "tilt_forward", "Keypad 8", "tilt the pad forward (front down)"},
    {Action::TiltBack, "tilt_back", "Keypad 2", "tilt the pad back (front up)"},
};
static_assert(std::size(kActions) == static_cast<size_t>(Action::Count));

std::array<std::vector<SDL_Scancode>, static_cast<size_t>(Action::Count)> g_bindings;

std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

std::vector<SDL_Scancode> ParseKeys(const std::string& list, const char* action) {
    std::vector<SDL_Scancode> keys;
    std::stringstream in(list);
    std::string name;
    while (std::getline(in, name, ',')) {
        name = Trim(name);
        if (name.empty()) continue;
        const SDL_Scancode code = SDL_GetScancodeFromName(name.c_str());
        if (code == SDL_SCANCODE_UNKNOWN)
            std::fprintf(stderr, "[input] %s: unknown key '%s' in %s\n", action, name.c_str(), kConfigPath);
        else
            keys.push_back(code);
    }
    return keys;
}

void WriteDefaultConfig() {
    CreateDirectoryA("user", nullptr);
    CreateDirectoryA("user\\config", nullptr);
    std::ofstream out(kConfigPath);
    if (!out) return;
    out << "# Keyboard controls. One action per line: action = key, key, ...\n"
           "# Key names are SDL's (A, Space, Left Shift, Return, Escape, Up, F1, Keypad 8, ...).\n"
           "# Delete this file to get the defaults back.\n\n";
    for (const auto& a : kActions) out << "# " << a.description << "\n" << a.name << " = " << a.defaults << "\n\n";
}

// Test runs (remote input "action <name> <ms>") hold actions without a keyboard.
std::array<uint64_t, static_cast<size_t>(Action::Count)> g_remoteUntil{};

// withKeyboard = false: only the remote holds (test runs), not the PC keyboard; see ApplyToFrame.
bool Held(Action action, bool withKeyboard = true) {
    if (SDL_GetTicks() < g_remoteUntil[static_cast<size_t>(action)]) return true;
    if (!withKeyboard) return false;
    if (!SDL_GetKeyboardFocus()) return false;  // only while the game window is active
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (!keys) return false;
    for (SDL_Scancode code : g_bindings[static_cast<size_t>(action)])
        if (keys[code]) return true;
    return false;
}

}  // namespace

void Init() {
    for (const auto& a : kActions) g_bindings[static_cast<size_t>(a.action)] = ParseKeys(a.defaults, a.name);
    std::ifstream in(kConfigPath);
    if (!in) {
        WriteDefaultConfig();
        std::fprintf(stderr, "[input] wrote default keyboard controls to %s\n", kConfigPath);
        return;
    }
    std::fprintf(stderr, "[input] keyboard controls from %s\n", kConfigPath);
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = Trim(line.substr(0, eq));
        bool known = false;
        for (const auto& a : kActions) {
            if (name != a.name) continue;
            g_bindings[static_cast<size_t>(a.action)] = ParseKeys(line.substr(eq + 1), a.name);
            known = true;
        }
        if (!known) std::fprintf(stderr, "[input] unknown action '%s' in %s\n", name.c_str(), kConfigPath);
    }
}

bool HoldForTest(const std::string& name, uint64_t ms) {
    for (const auto& a : kActions) {
        if (name != a.name) continue;
        g_remoteUntil[static_cast<size_t>(a.action)] = SDL_GetTicks() + ms;
        return true;
    }
    return false;
}

bool AnyHeld() {
    for (size_t i = 0; i < static_cast<size_t>(Action::Count); ++i)
        if (Held(static_cast<Action>(i))) return true;
    return false;
}

namespace {
// The Popit press that open_popit adds to the frame, and how long the computer stays blocked.
std::atomic<uint32_t> g_pulse{0};
std::atomic<uint64_t> g_pulseUntil{0};
uint64_t g_blockUntil = 0;  // RunGameHooks only
}  // namespace

void ApplyToFrame(ps4::PadData& frame, bool keys) {
    // Character movement: the walk and layer directions go through the left stick, which
    // is what the game's character controller reads, at full deflection.
    const int x = (Held(Action::MoveRight, keys) ? 1 : 0) - (Held(Action::MoveLeft, keys) ? 1 : 0);
    const int z = (Held(Action::LayerFront, keys) ? 1 : 0) - (Held(Action::LayerBack, keys) ? 1 : 0);
    if (x) frame.leftStick.x = x > 0 ? 255 : 0;
    if (z) frame.leftStick.y = z > 0 ? 255 : 0;
    if (Held(Action::Jump, keys)) frame.buttons |= ps4::PAD_CROSS;
    if (Held(Action::Grab, keys)) frame.buttons |= ps4::PAD_R1;

    // A Popit press from RunGameHooks (R), held for 150 ms.
    const uint32_t pulse = g_pulse.load();
    if (keys && pulse && SDL_GetTicks() < g_pulseUntil.load()) frame.buttons |= pulse;

    if (Held(Action::MenuUp, keys)) frame.buttons |= ps4::PAD_UP;
    if (Held(Action::MenuDown, keys)) frame.buttons |= ps4::PAD_DOWN;
    if (Held(Action::MenuLeft, keys)) frame.buttons |= ps4::PAD_LEFT;
    if (Held(Action::MenuRight, keys)) frame.buttons |= ps4::PAD_RIGHT;
    if (Held(Action::Confirm, keys)) frame.buttons |= ps4::PAD_CROSS;
    if (Held(Action::Back, keys)) frame.buttons |= ps4::PAD_CIRCLE;
    if (Held(Action::Pause, keys)) frame.buttons |= ps4::PAD_OPTIONS;
}

bool Tilt(float& roll, float& pitch, bool keys) {
    static const bool enabled = [] {
        const char* v = std::getenv("LBP3_TILT_KEYS");
        return !(v && std::string(v) == "off");
    }();
    roll = pitch = 0.f;
    if (!enabled) return false;
    roll = static_cast<float>((Held(Action::TiltRight, keys) ? 1 : 0) - (Held(Action::TiltLeft, keys) ? 1 : 0));
    pitch = static_cast<float>((Held(Action::TiltBack, keys) ? 1 : 0) - (Held(Action::TiltForward, keys) ? 1 : 0));
    return roll != 0.f || pitch != 0.f;
}

void RunGameHooks() {
    // The Pod computer and the Popit share one control in the game. Here each has its own:
    // open_computer calls the game's own "use the Pod computer" request directly (from
    // anywhere in the Pod), open_popit presses the Popit control with the computer request
    // blocked, so it never opens the computer. Neither does anything while a menu is up.
    // They act once per key press (holding R must not open and then close the Popit); the
    // block outlasts the press, because the Pod script reacts a few frames later.
    // Known gap: in reach of the computer the game itself doesn't open the Popit, so R
    // does nothing there.
    static bool computerWas = false, popitWas = false;
    const bool computerKey = Held(Action::OpenComputer), popitKey = Held(Action::OpenPopit);
    const bool computerPressed = computerKey && !computerWas, popitPressed = popitKey && !popitWas;
    computerWas = computerKey;
    popitWas = popitKey;
    if (computerPressed || popitPressed) {
        const bool menu = game_state::StartMenuOpen() || game_state::PodComputerInUse();
        const bool popit = game_state::PopitOpen();
        if (computerPressed && !menu && !popit) game_state::UsePodComputer();
        if (popitPressed && !menu) {
            g_pulse = popit ? ps4::PAD_CIRCLE : ps4::PAD_SQUARE;  // R toggles
            g_pulseUntil = SDL_GetTicks() + 150;
            game_state::BlockComputerFromSquare(true);
            g_blockUntil = SDL_GetTicks() + 1000;
        }
    }
    if (g_blockUntil && SDL_GetTicks() >= g_blockUntil) {
        game_state::BlockComputerFromSquare(false);
        g_blockUntil = 0;
    }
}

}  // namespace input::actions
