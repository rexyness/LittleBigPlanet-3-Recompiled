// "My Unlocks" offline (ticket 19): LBP3's settings page imports the LBP1 and LBP2 items that
// ship in LBP3's own game data, once the LBP servers report that the player's PSN account has
// played those games. The servers are gone (2024), so the page can never be used.
// The in-game menu (L3+R3 / Insert) arms the game's own import ("Import LBP1/LBP2 items"); it
// starts on the game's main thread the next time the start menu opens with a profile loaded, as
// the game's own page would start it. LBP3_MY_UNLOCKS=lbp1,lbp2 (either or both) arms it at
// launch instead (scripts, play.bat's MY_UNLOCKS); LBP3_MY_UNLOCKS=off installs nothing.
#pragma once

#include <cstdint>

namespace hle::my_unlocks {

// Hooks the start menu's IsOnline__ native (call before the game starts). Unless
// LBP3_MY_UNLOCKS=off the hook is always in place; while nothing is armed it only passes the
// call on. LBP3_MY_UNLOCKS_TRACE=1 also logs the import screen natives and IsOnline__'s calls.
void InstallHooks();

enum class State : int {
    Unavailable,  // no hook (LBP3_MY_UNLOCKS=off, IsOnline__ not found, no game loaded)
    Idle,         // nothing armed
    Armed,        // starts the next time the start menu opens with a profile loaded
    Running,      // an import this runtime started is on screen
    Done,         // it closed (the game said "successfully imported" or "Failed to import objects")
};

// Any thread (the in-game menu: presenter thread). Request arms LittleBigPlanet and all six
// LittleBigPlanet 2 story worlds; false when unavailable or an import is on screen. Running it on
// an already-imported profile adds nothing (ticket 19, unl5). Cancel disarms it before it starts.
bool Request();
void Cancel();
State GetState();

// The state from its parts (tests): the hook in place, the flags armed, an import this runtime
// started (or is starting), and the game's import screen flag.
State Decide(bool hooked, uint32_t armed, bool started, bool starting, bool screenActive);

// The import screen cancels the import ("Failed to import objects") on any frame in which a
// controller holds its back button: Circle on this US build, Cross where the game's region
// check picks it (FUN_008bee30, FUN_000638d0). Closing the start menu with Circle while the
// import runs therefore throws it away. While an import this runtime started is on screen,
// GuardImport takes that button out of what the game reads (scePadReadState); otherwise it
// returns `buttons` unchanged. LBP3_MY_UNLOCKS_GUARD=off keeps the game's behaviour.
uint32_t GuardImport(uint32_t buttons);

// The decision on its own (tests): `buttons` without the import's cancel button while it runs.
uint32_t FilterBack(uint32_t buttons, bool importRunning, bool backIsCircle);

}  // namespace hle::my_unlocks
