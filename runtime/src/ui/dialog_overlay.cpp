// Placeholder until the real dialogs land: never available, so hle/dialog_window.cpp keeps
// its native windows.
#include "ui/dialog_overlay.h"

namespace ui::dialog {

bool Available() { return false; }
uint64_t Show(Request, AnswerFn) { return 0; }
void SetProgress(uint64_t, uint32_t) {}
void Close(uint64_t) {}
bool Visible() { return false; }

}  // namespace ui::dialog
