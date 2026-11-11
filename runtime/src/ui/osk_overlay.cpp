// Placeholder until the real keyboard lands: never available, so libSceImeDialog keeps
// its native window.
#include "ui/osk_overlay.h"

namespace ui::osk {

bool Available() { return false; }
uint64_t Show(Request, AnswerFn) { return 0; }
void Close(uint64_t) {}
bool Visible() { return false; }
void Attach(SDL_Window*, SDL_Renderer*) {}
bool HandleEvent(const SDL_Event&) { return false; }
void Draw() {}

}  // namespace ui::osk
