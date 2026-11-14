// The boot loading screen: what the game window shows from the moment it opens until the game's
// first picture, instead of a black window. That span is the graphics start-up (the Vulkan device,
// the driver's pipeline cache, then the shader cache's preload: about 4-7 s normally, 25-40 s on
// the first start after a PC restart, while Windows reads ~10,000 small files) plus the game's own
// first seconds, which flip black frames.
//
// It looks like a PS4 starting a game: the game's own background art (sce_sys/pic1.png) and icon
// (icon0.png) from the user's dump, with a status line and a progress bar fed by the preload's
// counters (shadPS4 common/lbp3_boot.h). Without the art, a plain dark background. It takes no
// input and is drawn under every other overlay layer. It goes away (a short fade) at the first
// frame that isn't black, or 60 s after the first frame at the latest, and never comes back.
//
// Only the game shows it (main.cpp calls SetArt), not the test programs that share the overlay.
// LBP3_BOOT_SCREEN=0 turns it off (the old black window).
#pragma once

#include <filesystem>

#include <imgui.h>

namespace ui::boot {

// Main thread, before the game runs: where the art is (missing files are fine).
void SetArt(const std::filesystem::path& background, const std::filesystem::path& icon);

// The graphics start-up (hle/videoout.cpp, around gpu::Init): any thread.
void NoteGpuInit(bool finished);

// Presenter thread: a new frame from the game, 32-bit BGRA rows. Cheap (samples a grid) and
// only looks while the screen is up.
void NoteFrame(const void* pixels, unsigned width, unsigned height, unsigned pitchPixels);

}  // namespace ui::boot

// The overlay host's side (ui/overlay.cpp), presenter thread only.
namespace ui::boot::layer {
bool Wanted();               // the screen (or its fade) is up
void Frame(ImVec2 display);  // draws it on ImGui's background list
void Reset();                // the window changed: drop the textures
}  // namespace ui::boot::layer
