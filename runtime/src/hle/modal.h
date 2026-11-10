// System dialogs (text entry, messages, save data) are native windows here. On a
// PS4 the system takes the controller while one is up, so the game sees no
// buttons or stick movement until it closes. Dialogs call Begin when they open
// and End when they close; scePadReadState hands the game a neutral pad while
// any is open. Dialogs read the real pad through input::Read, which the gate
// doesn't touch.
#pragma once
#include <cstdint>

namespace hle::modal {

void Begin(const char* who);  // who: a short name for the log, e.g. "ime"
void End();
bool Active();

// For scePadReadState, with the real pad's buttons: true while the game should see a
// neutral pad. That is while a dialog is open, and after the last one closes until
// Cross, Circle, Enter and Esc are all released (at most 1 s): the game saw no presses
// while the dialog was up, so the press that answered it would otherwise reach the
// game as a fresh one and act on the menu behind it (and often reopen the dialog).
bool HoldsPad(uint32_t realButtons);

}  // namespace hle::modal
