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
#include <cstdio>
#include <string>

namespace gpu::dlss {

// The picture to read back instead of the display image.
struct Output {
    void* image = nullptr;  // VkImage, in TRANSFER_SRC_OPTIMAL; nullptr = read the display image as before
    uint32_t width = 0, height = 0;
    // For frame generation at the output size (gpu/fg_live.h OnUpscaled), same size and layout:
    void* hudless = nullptr;   // the picture before the screen HUD went back on top
    void* velocity = nullptr;  // the game's vectors resampled to the output size (null: none this frame)
};

#if LBP3_DLSS
// At start-up, before anything reads LBP3_RES: with LBP3_UPSCALER=dlss, checks that DLSS can run
// here (NVIDIA's DLL next to lbp3.exe, NGX's feature query on the GPU the game will use). If it
// can, LBP3_RES (the output size) becomes the render size of LBP3_UPSCALER_MODE (dlaa, quality,
// balanced, performance, ultraperformance; default quality). If it can't, the upscaler stays off
// and the game renders at the output size, as without it.
void Prepare();
// lbp3.exe --dlss-probe: one line on stdout; exit 0 = DLSS can run here, 1 = it can't, 2 = not a
// DLSS build.
int Probe();
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
// The remote command "dlss <key> <value>" (tests): jitter ++|+-|-+|--|on|off, mv
// normal|zero|flipx|flipy|flipxy, smaa skip|keep. Applied at the next flip, with a history reset.
void Remote(const std::string& key, const std::string& value);
// The output size Prepare kept when DLSS upscales from a smaller render size; 0x0 otherwise.
void OutputSize(uint32_t* w, uint32_t* h);
#else
inline void OutputSize(uint32_t* w, uint32_t* h) { *w = *h = 0; }
inline void Prepare() {}
inline int Probe() {
    std::printf("dlss: not a DLSS build\n");
    return 2;
}
inline void RegisterVulkanExtensions() {}
inline bool Start() { return false; }
inline bool Active() { return false; }
inline void SetDisplayBuffer(int, uint64_t) {}
inline Output Process(void*, void*) { return {}; }
inline void Remote(const std::string&, const std::string&) {}
#endif

}  // namespace gpu::dlss
