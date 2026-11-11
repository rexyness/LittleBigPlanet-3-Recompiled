// Host gamepad backend. Translates whatever controller the PC has into
// PS4 pad state, so the HLE libScePad layer never touches Windows APIs.
#pragma once
#include "hle/pad_types.h"

namespace input {

constexpr int kMaxPads = 4;

void Init();
// Fills `out` for the given player slot. Returns false if nothing is connected there.
// Runs no game code, so system dialogs may read the real pad with it from any thread.
// keyboard = false leaves out the PC keyboard's actions mapped onto slot 0 (Space = Cross,
// arrows = d-pad...), for the overlays, which take typed keys as key events; remote test
// holds ("action confirm") still count.
bool Read(int slot, ps4::PadData& out, bool keyboard = true);
// What the game's own pad read does besides reading: keyboard actions that call the game
// directly, and the opt-in cutscene watchdog. Game code: call it from the game's thread
// in scePadReadState, with no lock held, and not while a system dialog has the controller.
void RunGameHooks(int slot);
void SetVibration(int slot, uint8_t largeMotor, uint8_t smallMotor);
void SetLightBar(int slot, uint8_t r, uint8_t g, uint8_t b);

}  // namespace input
