// The in-game menu (best-of-both-worlds branch): a layer of the overlay host (ui/overlay.h) that
// opens with Insert or L3+R3 (pressed together) and shows the live stats (frame rate, game work,
// frame generation, display latency) with a few switches: the corner stats overlay, frame
// generation on/off live, the volume, and Quit game (press twice). While it's open the game sees
// a neutral pad (hle/modal.h), as with the system dialogs; Circle, Options, Esc, Insert or
// L3+R3 close it. LBP3_MENU=off turns the hotkeys off.
//
// The corner stats overlay (one line, top left) stays on screen after the menu closes;
// LBP3_STATS=1 turns it on at start.
#pragma once

namespace ui::menu {

// LBP3_MENU (read once; on unless "off" or "0").
bool Enabled();
// Presenter thread: open or close it (ignored while a keyboard or dialog is up).
void Toggle();
bool IsOpen();

}  // namespace ui::menu
