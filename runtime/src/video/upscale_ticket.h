// An upscaled copy of a frame the window shows (LBP3_UPSCALE=fsr1; gpu/upscale.h): submitted
// on the GPU with the frame, fetched later by whoever presents it (the presenter, or frame
// generation's worker), off the command-processor thread.
#pragma once
#include <cstdint>

namespace video {

struct UpscaleTicket {
    uint64_t id = 0;    // 0 = no upscaled copy of this frame
    uint64_t tick = 0;  // the GPU scheduler tick its work completes with
    int kind = 0;       // 0 = a real frame, 1 = a generated one
    int slot = 0;       // its readback slot
    uint32_t width = 0, height = 0;
};

}  // namespace video
