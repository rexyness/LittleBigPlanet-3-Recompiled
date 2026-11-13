// Keyboard (and later mouse) controls as named game actions, not as an emulated pad.
//
// Each action ("jump", "open_popit", ...) is bound to keys in user/config/input.ini and
// has its own behaviour below. Until the game's own functions for an action are found
// (character movement, opening the Popit or the Pod computer), that behaviour feeds the
// game's per-frame control input, the only way into the game we have so far; actions are
// moved over to direct game calls one at a time without changing bindings.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "hle/pad_types.h"

namespace input::actions {

enum class Action {
    // Character
    MoveLeft,
    MoveRight,
    LayerBack,   // move one layer away from the camera
    LayerFront,  // move one layer towards the camera
    Jump,
    Grab,
    // Pod and creation
    OpenComputer,
    OpenPopit,
    // Menus
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    Confirm,
    Back,
    Pause,
    // Motion: tilt the pad without motion sensors (input/motion.h). Off with LBP3_TILT_KEYS=off.
    TiltLeft,
    TiltRight,
    TiltForward,  // front of the pad down
    TiltBack,     // front of the pad up
    Count,
};

// Loads bindings (writing a commented default file the first time).
void Init();
// The pad_<button> = <pad buttons> lines input.ini had (Init reads them; input/action_table.h).
const std::vector<std::pair<std::string, std::string>>& PadLines();
// True while the game window has keyboard focus and some binding is held.
bool AnyHeld();
// Holds an action for `ms` as if its key were down (remote-controlled test runs).
// Returns false for an unknown action name.
bool HoldForTest(const std::string& name, uint64_t ms);
// Applies the held actions to this frame's control input for player 1. Runs no game code,
// so system dialogs can read the pad through it from any thread. keys = false applies only
// the remote holds (test runs) and not the PC keyboard, for the overlays, which take typed
// keys as key events.
void ApplyToFrame(ps4::PadData& frame, bool keys = true);
// The actions that call the game directly (open_computer, and open_popit's block on the
// computer). Game code: only from the game's own pad read, with no input lock held, and
// not while a system dialog has the controller.
void RunGameHooks();
// The virtual tilt the tilt actions ask for: roll (+1 = right side down) and pitch (+1 = front
// up), each -1, 0 or 1. Same keyboard rules as ApplyToFrame (keys = false: remote holds only).
// Returns true while any tilt action is held. Always false with LBP3_TILT_KEYS=off.
bool Tilt(float& roll, float& pitch, bool keys = true);

}  // namespace input::actions
