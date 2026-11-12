// Frame generation on the GPU, in game (LBP3_FRAMEGEN=1 LBP3_FRAMEGEN_MODE=warp; perf/fg_gpu.md).
//
// On the command-processor thread, copies only:
//  - after the composite (the first draw with a pixel shader into a display buffer), the
//    display image goes into the generator's HUD-less buffer;
//  - at the game's 007f7f00 masked fill (the velocity clear, after its motion blur used it), the
//    velocity target goes into the generator's vector buffer;
//  - in the flip's readback (ReadbackDisplayBuffer), the display image also goes into the
//    generator's frame buffer.
// After the readback's Finish, Kick records the generation into a command buffer of its own and
// submits it without waiting; the frame-generation worker later waits for it (Fetch) and takes
// the result. The game's images are never written, and the game-facing flip path is unchanged.
#pragma once
#include <cstdint>
#include <vector>

#include "video/framegen_warp.h"

namespace gpu::fglive {

using Ticket = video::framegen::GpuTicket;  // id 0 = no generation for this frame

// Turns the GPU generator on with these parameters (from the game thread, before flips).
void Configure(const video::framegen::WarpParams& p);
bool Active();

void SetDisplayBuffer(int index, uint64_t address);

// From ReadbackDisplayBuffer, after its copy was recorded (image in transfer-source layout):
// cmd = VkCommandBuffer, image = VideoCore::Image*.
void OnReadbackCopy(void* cmd, void* image);

// After the flip's readback (its Finish done): submits the generation for this frame.
Ticket Kick();

struct Fetched {
    bool ok = false;
    bool declined = false;
    bool usedVectors = false, usedHudless = false;
    double gpuMs = -1;
    double waitMs = 0;
    uint32_t covered = 0, saturated = 0, total = 0;
    uint32_t pixText = 0;  // white pixels near in-world text drawn moved (framegen_warp.h textKeep)
};
// From the worker: waits for the ticket's work and copies the generated frame into out.
Fetched Fetch(const Ticket& t, std::vector<uint8_t>& out);

}  // namespace gpu::fglive
