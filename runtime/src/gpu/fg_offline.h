// Frame generation on the GPU, offline (fgeval, framegentest): the same compute passes on a
// headless device of its own, fed from host frames. Synchronous.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "gpu/fg_gpu.h"

namespace gpu::fggpu {

struct OfflineInputs {
    const void* prev = nullptr;  // N-1, 4 bytes per pixel
    const void* cur = nullptr;   // N
    const void* hudPrev = nullptr;
    const void* hudCur = nullptr;
    const void* vel = nullptr;  // N's velocity target
    uint32_t width = 0, height = 0;
    float t = 0.5f, motionScale = 1.0f;
};

class OfflineRunner {
public:
    explicit OfflineRunner(const video::framegen::WarpParams& p);
    ~OfflineRunner();
    bool Ok() const;
    bool Run(const OfflineInputs& in, std::vector<uint8_t>& out, Result& res);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gpu::fggpu
