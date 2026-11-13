// The controls of user\config\input.ini, shared by the runtime (input/actions.cpp, the keyboard;
// input/gamepad_sdl.cpp, the pad) and the launcher's controls editor, so the two can't drift apart.
//   keyboard:  <action> = <SDL key name>, ...      e.g. jump = Space
//   pad:       pad_<button> = <pad button>, ...     e.g. pad_cross = south (the bottom face button)
// The pad lines are optional: without them each PS4 button comes from its own pad button, as
// before (positional: south/east/west/north = Cross/Circle/Square/Triangle on any pad).
#pragma once

#include <SDL3/SDL_gamepad.h>

#include <cstring>

namespace input::actions {

// In the order of input::actions::Action.
struct ActionRow {
    const char* name;         // key in input.ini
    const char* defaults;     // comma-separated SDL key names
    const char* description;
};
inline constexpr ActionRow kActionRows[] = {
    {"move_left", "A", "walk left"},
    {"move_right", "D", "walk right"},
    {"layer_back", "W", "step one layer back (away from the camera)"},
    {"layer_front", "S", "step one layer forward (towards the camera)"},
    {"jump", "Space", "jump (hold to jump higher)"},
    {"grab", "Left Shift", "grab and hold on"},
    {"open_computer", "E", "use the Pod computer"},
    {"open_popit", "R", "open the Popit"},
    {"menu_up", "Up", "menus: move up"},
    {"menu_down", "Down", "menus: move down"},
    {"menu_left", "Left", "menus: move left"},
    {"menu_right", "Right", "menus: move right"},
    {"confirm", "Return", "menus: select"},
    {"back", "Escape, Backspace", "menus: back / close"},
    {"pause", "Tab", "pause menu"},
    {"tilt_left", "Keypad 4", "tilt the pad left (for pads without motion sensors, and the keyboard)"},
    {"tilt_right", "Keypad 6", "tilt the pad right"},
    {"tilt_forward", "Keypad 8", "tilt the pad forward (front down)"},
    {"tilt_back", "Keypad 2", "tilt the pad back (front up)"},
};

// The PS4 buttons a pad button can be mapped to, with their defaults.
struct PadRow {
    const char* name;         // key in input.ini
    const char* defaults;     // comma-separated pad button names (PadButtonNames)
    const char* label;        // for people
};
inline constexpr PadRow kPadRows[] = {
    {"pad_cross", "south", "Cross"},
    {"pad_circle", "east", "Circle"},
    {"pad_square", "west", "Square"},
    {"pad_triangle", "north", "Triangle"},
    {"pad_l1", "leftshoulder", "L1"},
    {"pad_r1", "rightshoulder", "R1"},
    {"pad_l3", "leftstick", "L3 (press the left stick)"},
    {"pad_r3", "rightstick", "R3 (press the right stick)"},
    {"pad_options", "start", "Options"},
    {"pad_touchpad", "touchpad, back", "Touch pad press"},
    {"pad_up", "dpup", "D-pad up"},
    {"pad_down", "dpdown", "D-pad down"},
    {"pad_left", "dpleft", "D-pad left"},
    {"pad_right", "dpright", "D-pad right"},
};

// The pad button names input.ini uses (our own, positional), and SDL's buttons for them.
struct PadButtonName {
    const char* name;
    SDL_GamepadButton button;
    const char* label;  // for people
};
inline constexpr PadButtonName kPadButtonNames[] = {
    {"south", SDL_GAMEPAD_BUTTON_SOUTH, "bottom face button (Cross / A)"},
    {"east", SDL_GAMEPAD_BUTTON_EAST, "right face button (Circle / B)"},
    {"west", SDL_GAMEPAD_BUTTON_WEST, "left face button (Square / X)"},
    {"north", SDL_GAMEPAD_BUTTON_NORTH, "top face button (Triangle / Y)"},
    {"leftshoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "left shoulder (L1 / LB)"},
    {"rightshoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "right shoulder (R1 / RB)"},
    {"leftstick", SDL_GAMEPAD_BUTTON_LEFT_STICK, "left stick press"},
    {"rightstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK, "right stick press"},
    {"start", SDL_GAMEPAD_BUTTON_START, "Options / Start / Menu"},
    {"back", SDL_GAMEPAD_BUTTON_BACK, "Share / Back / View"},
    {"guide", SDL_GAMEPAD_BUTTON_GUIDE, "PS / Home / Guide"},
    {"touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD, "touch pad press"},
    {"dpup", SDL_GAMEPAD_BUTTON_DPAD_UP, "d-pad up"},
    {"dpdown", SDL_GAMEPAD_BUTTON_DPAD_DOWN, "d-pad down"},
    {"dpleft", SDL_GAMEPAD_BUTTON_DPAD_LEFT, "d-pad left"},
    {"dpright", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "d-pad right"},
    {"misc1", SDL_GAMEPAD_BUTTON_MISC1, "mic / capture / extra button"},
    {"paddle1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, "back paddle (right 1)"},
    {"paddle2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, "back paddle (left 1)"},
    {"paddle3", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, "back paddle (right 2)"},
    {"paddle4", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, "back paddle (left 2)"},
};

// "south" -> SDL_GAMEPAD_BUTTON_SOUTH; SDL_GAMEPAD_BUTTON_INVALID if unknown ("none" included).
inline SDL_GamepadButton PadButtonFromName(const char* name) {
    for (const auto& b : kPadButtonNames)
        if (std::strcmp(b.name, name) == 0) return b.button;
    return SDL_GAMEPAD_BUTTON_INVALID;
}
inline const char* PadButtonName(SDL_GamepadButton button) {
    for (const auto& b : kPadButtonNames)
        if (b.button == button) return b.name;
    return nullptr;
}

}  // namespace input::actions
