// FSR 1 (EASU + RCAS) as Vulkan compute passes on storage buffers (gpu/fg_shaders/upscale.comp;
// LBP3_UPSCALE=fsr1, perf/upscale.md in the planning repo).
//
// UpscaleGpu owns its buffers and pipelines on a given device. The caller copies a frame into
// InputBuffer (in game: a GPU-side copy of the display image; offline: an upload), or points it
// at a buffer of its own (frame generation's in-between frame), records the passes with Record
// into a command buffer of its own (never inside a game command buffer), submits it, and after it
// completed reads the result from a host-visible readback slot. Nothing here writes the game's
// images.
#pragma once
#include <cstdint>
#include <memory>

#include "gpu/fg_gpu.h"  // fggpu::Device

namespace gpu::upgpu {

class UpscaleGpu {
public:
    static constexpr int kKinds = 2;  // 0 = real frames, 1 = generated frames: separate slot rings
    static constexpr int kSlots = 3;

    explicit UpscaleGpu(const fggpu::Device& dev);
    ~UpscaleGpu();
    UpscaleGpu(const UpscaleGpu&) = delete;
    UpscaleGpu& operator=(const UpscaleGpu&) = delete;

    bool Ok() const;
    // (Re)allocates for frames of inW x inH upscaled to outW x outH. False on failure (then not Ready).
    bool Resize(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH);
    bool Ready() const;
    uint32_t InW() const;
    uint32_t InH() const;
    uint32_t OutW() const;
    uint32_t OutH() const;
    uint64_t OutBytes() const;

    // Device-local, one uint per pixel (inW x inH), the image's bytes as stored: a transfer target.
    void* InputBuffer() const;  // VkBuffer

    // Offline: records a copy of host pixels (inW * inH * 4 bytes) into InputBuffer through a staging buffer.
    void RecordUpload(void* cmd, const void* pixels);

    // Records the upscale of src (a VkBuffer of inW x inH uints; nullptr = InputBuffer) into readback
    // slot (kind, slot). sharpness: RCAS's, in stops (0 = sharpest). Makes earlier transfer and compute
    // writes to src visible first, and waits for earlier submissions' use of its own buffers.
    void Record(void* cmd, void* src, int kind, int slot, float sharpness);

    // After the recorded work completed: the upscaled frame (OutBytes, host memory) and the GPU time of
    // its passes (-1 = not available).
    const uint8_t* Output(int kind, int slot);
    double GpuMs(int kind, int slot);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gpu::upgpu
