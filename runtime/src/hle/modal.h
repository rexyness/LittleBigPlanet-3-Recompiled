// System dialogs (text entry, messages, save data) are native windows here. On a
// PS4 the system takes the controller while one is up, so the game sees no
// buttons or stick movement until it closes. Dialogs call Begin when they open
// and End when they close; scePadReadState hands the game a neutral pad while
// any is open. Dialogs read the real pad through input::Read, which the gate
// doesn't touch.
#pragma once

namespace hle::modal {

void Begin(const char* who);  // who: a short name for the log, e.g. "ime"
void End();
bool Active();

}  // namespace hle::modal
