// Quitting the game from inside it (best-of-both-worlds branch): the in-game menu's "Quit game"
// and "Quit Game" in the game's own pause menu.
//
// RequestQuit closes the game the way the window's close button does (the presenter's quit path:
// "[video] window closed", then the process ends), but first waits, up to 15 s, until no save
// data is mounted, so a save being written is finished.
//
// The pause menu (LBP3_QUIT_MENU=1, with LBP3_FILE_EDITS pointing at tools/quit_menu.py's
// description): the script edit makes the menu's Good Grief! button (online-only, greyed out
// offline) ask the script native LAMS.Translate (Translate__i, eboot+0x839aa0) for two keys of
// ours, which the hook here answers: kQuitLabel is the button's text ("Quit Game", or "Press
// again to quit" for 3 s after a first press) and kQuitAction a press. The first press arms it,
// the second quits. The approach, the two keys and the three-second confirmation are LBPPC's
// (emurphy04, aad6a91: scripts/quit_menu.py, GPL-2.0-or-later); this hook is our own, on our
// script-native binding table.
#pragma once
#include <cstdint>

namespace hle::quit {

constexpr int32_t kQuitLabel = 0x4C425051;   // 'LBPQ'
constexpr int32_t kQuitAction = 0x4C425058;  // 'LBPX'

// Any thread. Only the first call does anything.
void RequestQuit(const char* why);
bool QuitRequested();

// LBP3_QUIT_MENU=1: hooks Translate__i (after the executable is linked, before the game runs).
void InstallPauseMenuQuit();

// The pause-menu button's logic, for the test: the text for `key` at `nowMs`, given when a
// first press armed it (0 = not armed). For kQuitAction, `pressArmedUntil` is updated and
// `quit` set on the confirming press. Other keys return nullptr (the game's own text).
const char16_t* PauseMenuText(int32_t key, uint64_t nowMs, uint64_t& armedUntil, bool& quit);

}  // namespace hle::quit
