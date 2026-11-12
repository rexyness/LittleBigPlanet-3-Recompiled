// The window's upscaler offline (upscaletest, upeval): the same FSR 1 passes on a headless
// device of its own, fed from host frames. Synchronous.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace gpu::upgpu {

class OfflineUpscaler {
public:
    OfflineUpscaler();
    ~OfflineUpscaler();
    bool Ok() const;
    // Upscales in (inW x inH, 4 bytes per pixel) to outW x outH with RCAS sharpness `sharpness`
    // (stops). gpuMs: the passes' GPU time (-1 = not available).
    bool Run(const void* in, uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, float sharpness,
             std::vector<uint8_t>& out, double& gpuMs);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gpu::upgpu
