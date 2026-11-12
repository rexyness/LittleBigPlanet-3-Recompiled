// "My Unlocks" offline (ticket 19): LBP3's settings page imports the LBP1 and LBP2 items that
// ship in LBP3's own game data, once the LBP servers report that the player's PSN account has
// played those games. The servers are gone (2024), so the page can never be used.
// LBP3_MY_UNLOCKS=lbp1,lbp2 (either or both) runs the game's own import once per launch, the
// first time the start menu opens with a profile loaded.
#pragma once

#include <cstdint>

namespace hle::my_unlocks {

// Hooks the start menu's IsOnline__ native when LBP3_MY_UNLOCKS is set (call before the game
// starts). LBP3_MY_UNLOCKS_TRACE=1 also logs the import screen natives.
void InstallHooks();

// The import screen cancels the import ("Failed to import objects") on any frame in which a
// controller holds its back button: Circle on this US build, Cross where the game's region
// check picks it (FUN_008bee30, FUN_000638d0). Closing the start menu with Circle while the
// import runs therefore throws it away. While an import this switch started is on screen,
// GuardImport takes that button out of what the game reads (scePadReadState); with the switch
// off it returns `buttons` unchanged. LBP3_MY_UNLOCKS_GUARD=off keeps the game's behaviour.
uint32_t GuardImport(uint32_t buttons);

// The decision on its own (tests): `buttons` without the import's cancel button while it runs.
uint32_t FilterBack(uint32_t buttons, bool importRunning, bool backIsCircle);

}  // namespace hle::my_unlocks
