// Frame generation on the GPU (LBP3_FRAMEGEN_MODE=warp; perf/fg_gpu.md in the planning repo):
// lane D's warp generator (video/framegen_warp.cpp) as Vulkan compute passes, same integer math.
//
// WarpGpu owns its buffers and pipelines on a given device. The caller copies the frames into
// FinalBuffer/HudBuffer/VelBuffer (in game: GPU-side copies of the game's images; offline:
// uploads), records the generation with Record into a command buffer of its own (never inside a
// game command buffer), submits it, and after it completed reads the result from a host-visible
// readback slot. Nothing here writes the game's images.
#pragma once
#include <cstdint>
#include <memory>

#include "video/framegen_warp.h"  // WarpParams

namespace gpu::fggpu {

// The device to run on: shadPS4's (in game) or one made for the offline tools.
struct Device {
    void* device = nullptr;          // VkDevice
    void* physicalDevice = nullptr;  // VkPhysicalDevice
    void* allocator = nullptr;       // VmaAllocator
    uint32_t queueFamily = 0;
};

struct FrameFlags {
    int cur = 0;  // the slot holding N; N-1 is in cur ^ 1
    bool hudPrev = false, hudCur = false;  // HUD-less images of N-1 / N were copied
    bool vel = false;                      // N's velocity target was copied
    float t = 0.5f;
    float motionScale = 1.0f;
};

struct Result {
    uint32_t total = 0, covered = 0, saturated = 0;  // the game's vectors (level-0 sample grid)
    uint32_t hudHit = 0, hudN = 0;
    bool usedHudless = false, usedVectors = false;
    uint32_t pixAvg = 0, pixAlt = 0, pixFall = 0, pixStatic = 0, pixHud = 0;
    double gpuMs = -1;  // timestamps around the passes (-1 = not available)
    bool declined = false;  // most covered vectors saturated (a cut): don't show it
};

class WarpGpu {
public:
    static constexpr int kReadSlots = 3;

    WarpGpu(const Device& dev, const video::framegen::WarpParams& p);
    ~WarpGpu();
    WarpGpu(const WarpGpu&) = delete;
    WarpGpu& operator=(const WarpGpu&) = delete;

    // (Re)allocates for frames of w x h (4 bytes per pixel). False on failure.
    bool Resize(uint32_t w, uint32_t h);
    uint32_t Width() const;
    uint32_t Height() const;
    bool Ready() const;

    // Device-local buffers the inputs go into: one uint per pixel, the image's bytes as stored.
    void* FinalBuffer(int slot) const;  // VkBuffer
    void* HudBuffer(int slot) const;
    void* VelBuffer() const;
    uint64_t FrameBytes() const;

    // Offline: records a copy of host pixels (FrameBytes) into dst through a staging buffer
    // (index 0..4 picks the staging region; the data is copied at once).
    void RecordUpload(void* cmd, void* dst, const void* pixels, int index);

    // Records the generation of the in-between frame of N-1 and N into cmd (VkCommandBuffer),
    // with its result going to readback slot readSlot. Makes earlier transfer writes to the input
    // buffers visible first.
    void Record(void* cmd, const FrameFlags& f, int readSlot);

    // After the recorded work completed: the generated frame (FrameBytes, host memory) and the
    // counters of readback slot readSlot.
    const uint8_t* Output(int readSlot);
    Result ReadResult(int readSlot);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gpu::fggpu
