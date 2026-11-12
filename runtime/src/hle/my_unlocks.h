// "My Unlocks" offline (ticket 19): LBP3's settings page imports the LBP1 and LBP2 items that
// ship in LBP3's own game data, once the LBP servers report that the player's PSN account has
// played those games. The servers are gone (2024), so the page can never be used.
// LBP3_MY_UNLOCKS=lbp1,lbp2 (either or both) runs the game's own import once per launch, the
// first time the start menu opens with a profile loaded.
#pragma once

namespace hle::my_unlocks {

// Hooks the start menu's IsOnline__ native when LBP3_MY_UNLOCKS is set (call before the game
// starts). LBP3_MY_UNLOCKS_TRACE=1 also logs the import screen natives.
void InstallHooks();

}  // namespace hle::my_unlocks
