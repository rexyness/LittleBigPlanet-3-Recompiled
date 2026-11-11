// The overlay host: one Dear ImGui context on the presenter's SDL renderer, shared by
// everything the runtime draws over the game: the on-screen keyboard (ui/osk_overlay.h) and
// the system dialogs (ui/dialog_overlay.h), its two layers.
//
// The presenter calls Attach once, every window event through HandleEvent, and Draw between
// the game frame and SDL_RenderPresent. Every layer that is up is drawn, oldest first; only
// the newest one (by the time it was shown) takes the pad, the keys and the mouse. A layer
// that comes back on top ignores whatever is still held until it is released. With nothing
// up, HandleEvent and Draw cost a couple of atomic loads.
#pragma once

union SDL_Event;
struct SDL_Window;
struct SDL_Renderer;

namespace ui::overlay {

// True once a game window is presenting (any thread).
bool Available();

// ---- Presenter thread only ----
void Attach(SDL_Window* window, SDL_Renderer* renderer);
// True = an overlay used the event, so it is not a game hotkey. Always false with nothing up.
bool HandleEvent(const SDL_Event& e);
void Draw();

}  // namespace ui::overlay
