#pragma once
// The DLSS upscaler of a DLSS build (issue #24; docs/adr/0002, 0003). NVIDIA's SDK is linked in
// only when the build switch LBP3_DLSS is on; in a public build everything here is an empty
// inline stub.
//
// At run time it is on when LBP3_UPSCALER=dlss. Per frame it copies the game's HUD-less image
// (the display buffer right after the final composite), its velocity target and its scene depth
// as they go by, and at the flip runs DLSS on them, puts the screen HUD back on top from the
// finished frame, and hands that picture to the readback instead of the display buffer.

#include <cstdint>

namespace gpu::dlss {

// The picture to read back instead of the display image.
struct Output {
    void* image = nullptr;  // VkImage, in TRANSFER_SRC_OPTIMAL; nullptr = read the display image as before
    uint32_t width = 0, height = 0;
};

#if LBP3_DLSS
// Before the Vulkan instance is created: with LBP3_UPSCALER=dlss, has shadPS4 enable the
// instance and device extensions NGX asks for.
void RegisterVulkanExtensions();
// After the device exists: starts NGX and checks that DLSS can run. False (logged) when not.
bool Start();
bool Active();
void SetDisplayBuffer(int index, uint64_t address);
// At the flip, in the frame's command buffer, after the display image has been made a transfer
// source: records DLSS and the HUD's put-back. displayImage is the texture cache's Image.
Output Process(void* cmd, void* displayImage);
#else
inline void RegisterVulkanExtensions() {}
inline bool Start() { return false; }
inline bool Active() { return false; }
inline void SetDisplayBuffer(int, uint64_t) {}
inline Output Process(void*, void*) { return {}; }
#endif

}  // namespace gpu::dlss
