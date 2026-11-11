// Internal: what the overlay host (ui/overlay.cpp) and its layers (ui/osk_overlay.cpp,
// ui/dialog_overlay.cpp) need from each other. Presenter thread only, except NextStamp.
#pragma once
#include <cstdint>

#include <imgui.h>

union SDL_Event;
struct SDL_Window;
struct SDL_Renderer;

namespace ui::overlay::detail {

// A show-order stamp (any thread): the newest layer item takes the input.
uint64_t NextStamp();

// Sets up ImGui on the attached window the first time a layer needs it.
void EnsureImGui();
bool ImGuiReady();
SDL_Window* Window();
SDL_Renderer* Renderer();
ImFont* Font();     // Segoe UI (+ Segoe UI Symbol)
ImFont* KeyFont();  // Segoe UI Semibold (+ Segoe UI Symbol)

// When a layer item goes away: a click that answered leaves its button down in ImGui and the
// mouse captured (the SDL3 backend captures while a button is down).
void ReleaseMouse();

// The dim drawn once behind all layers.
constexpr ImU32 kDim = IM_COL32(0, 0, 0, 96);

}  // namespace ui::overlay::detail

// Each layer: Wanted (cheap, may be true before Sync picks a Show up), Sync (picks up Show and
// Close from other threads; returns the stamp of what is up, 0 = nothing), Poll (the pad, once
// a frame before ImGui's frame; `top` = this layer takes input; false once it answered),
// Event (window events, top layer only), Frame (inside ImGui's frame: the mouse if top, then
// drawing), Reset (the window changed: drop what is up without answering).
namespace ui::osk::layer {
bool Wanted();
uint64_t Sync();
bool Poll(bool top);
bool Event(const SDL_Event& e);
void Frame(bool top, ImVec2 display);
void Reset();
}  // namespace ui::osk::layer

namespace ui::dialog::layer {
bool Wanted();
uint64_t Sync();
bool Poll(bool top);
bool Event(const SDL_Event& e);
void Frame(bool top, ImVec2 display);
void Reset();
}  // namespace ui::dialog::layer
