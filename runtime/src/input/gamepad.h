// Host gamepad backend. Translates whatever controller the PC has into
// PS4 pad state, so the HLE libScePad layer never touches Windows APIs.
//
// Player slots (local co-op): slot 0 is player 1, who always has the PC keyboard too, with or
// without a controller. A controller in slot N >= 1 is player N+1, signed in while it is
// connected (hle/system_services.cpp turns slot changes into login/logout events).
// Seating: the first controller of the session becomes player 1's (PlayStation pads first when
// several are there at once; LBP3_PLAYER1_PAD=<part of a name> pins one), and slot 0 is kept
// for it while it is away, so a guest pad (e.g. Parsec's emulated Xbox pad) never becomes
// player 1 by connecting at the wrong moment. Other controllers take the lowest free slot from
// 1, so a guest who drops out and comes back gets slot 1 again if it is still free.
#pragma once
#include <cstdint>
#include <string>

#include "hle/pad_types.h"

namespace input {

constexpr int kMaxPads = 4;

void Init();
// True while a slot has a controller, real or virtual. Slot 0 always counts (the keyboard).
// `generation` changes whenever the slot's occupant does, so a pad that dropped out and came
// back between two looks still shows as a change.
bool SlotPresent(int slot, uint32_t* generation = nullptr);
// What is in a slot, for the log: the controller's name, "virtual pad", or "keyboard".
std::string SlotName(int slot);
// Picks up controllers that were connected or disconnected (pad reads do this too).
void Refresh();
// Test runs: n-1 virtual controllers (connected, neutral, driven by remote input) in the first
// free slots from 1, so players 2..n sign in without hardware. n = 1 removes them. Read once
// from LBP3_VIRTUAL_PADS=<n> at Init; the remote command "pads <n>" changes it at run time.
void SetVirtualPlayers(int players);
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
