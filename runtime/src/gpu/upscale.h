// The window's upscaler, in game (LBP3_UPSCALE=fsr1; perf/upscale.md in the planning repo).
//
// The game renders at 1920x1080; the window stretched that with plain bilinear filtering. With
// LBP3_UPSCALE=fsr1 and a window bigger than the frame, every frame the window shows is upscaled
// on the GPU to the window's size in pixels with FSR 1 (EASU, then RCAS sharpening;
// LBP3_UPSCALE_SHARP = RCAS's sharpness in stops, 0 = sharpest, default 0.25):
//  - a real frame: in the flip's readback (ReadbackDisplayBuffer) the display image is also
//    copied into the upscaler's input buffer; after the readback's Finish, KickReal records the
//    passes into a command buffer of its own and submits it without waiting;
//  - with frame generation's GPU warp, the in-between frame: RecordGen appends the passes to the
//    generator's own submission, reading its output buffer.
// The presenter (without frame generation) or frame generation's worker later waits for the
// work and takes the upscaled frame (Fetch), off the command-processor thread. The 1080p frame
// still goes everywhere it went (the frame dump, the photo capture, frame generation's inputs).
// Copies only: the game's images are only read (transfer source), and the game-facing flip path
// is unchanged apart from the copy and a submit.
#pragma once
#include <cstdint>
#include <vector>

#include "gpu/upscale_size.h"
#include "video/upscale_ticket.h"

namespace gpu::upscale {

// Reads LBP3_UPSCALE / LBP3_UPSCALE_SHARP (from the game thread, before flips). allowed = false
// keeps it off whatever the setting (frame generation with a generator that runs on the CPU,
// whose in-between frames couldn't be upscaled to match).
void ConfigureFromEnv(bool allowed);
bool Enabled();
float Sharpness();

// From the presenter, every refresh: the window's client size in pixels (0 x 0 when minimised).
void SetWindowPixels(uint32_t width, uint32_t height);

// From ReadbackDisplayBuffer, after its copy was recorded (image in transfer-source layout):
// cmd = VkCommandBuffer, image = VideoCore::Image*. Copies the image into the input buffer when
// the window is bigger than the frame.
void OnReadbackCopy(void* cmd, void* image);

// After the flip's readback (its Finish done), before frame generation's Kick: applies a
// changed window size (after waiting for the upscaler's work in flight).
void BeginFrame();

// Into frame generation's submission, after its in-between frame was recorded into src (a
// VkBuffer of the frame's size, one uint per pixel). Returns the ticket (id 0 = not upscaled).
video::UpscaleTicket RecordGen(void* cmd, void* src, uint32_t width, uint32_t height);
// Frame generation's submission went out at this tick (the generated ticket's tick).
void SetGenTick(video::UpscaleTicket& t, uint64_t tick);

// After BeginFrame (and frame generation's Kick): submits the real frame's upscale.
video::UpscaleTicket KickReal();

// From the presenter or the worker: waits for the ticket's work and copies the upscaled frame
// into out. False = not available (then show the frame unscaled).
bool Fetch(const video::UpscaleTicket& t, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height);

// The status line ([upscale] ...), printed with the [video] line; nothing when off.
void PrintStatus();

}  // namespace gpu::upscale
