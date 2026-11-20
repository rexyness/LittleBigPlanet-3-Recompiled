#pragma once
// The DLSS upscaler's modes and the render size each gives (issue #24). Shared by the runtime
// (gpu/dlss.cpp) and the launcher, so the launcher offers exactly what the game will do. No NVIDIA
// code here: it builds in every build.

#include <cmath>
#include <cstdint>
#include <cstring>

namespace dlss_modes {

struct Mode {
    const char* key;    // LBP3_UPSCALER_MODE and launcher.ini
    const char* label;  // shown to the player
    double ratio;       // output size / render size, per side (NVIDIA's guide 3.7.1)
};

inline constexpr Mode kModes[] = {
    {"dlaa", "DLAA", 1.0},
    {"quality", "Quality", 1.5},
    {"balanced", "Balanced", 1.724},
    {"performance", "Performance", 2.0},
    {"ultraperformance", "Ultra Performance", 3.0},
};
inline constexpr int kCount = 5;
inline constexpr int kDefault = 1;  // Quality

// The mode with that key (case-insensitive), or -1.
inline int Find(const char* key) {
    if (!key) return -1;
    for (int i = 0; i < kCount; ++i) {
        const char* a = kModes[i].key;
        const char* b = key;
        while (*a && *b && (*a | 0x20) == (*b | 0x20)) ++a, ++b;
        if (!*a && !*b) return i;
    }
    return -1;
}

// The render size for an output size: as big as output / ratio allows with the height a multiple
// of 8 and the width a multiple of 16 (the game's targets are read in 16-pixel rows:
// .scratch/lbp3-port/perf/capture_pitch.md), and an aspect never narrower than the output's and at
// most 0.9% wider, so a 16:9 output stays 16:9 to the port (core/res_patches.cpp's Wide()). False
// when that falls under 1280x720, where the game switches to its low-resolution UI and the port
// renders no smaller (common/lbp3_res.h). DLAA (ratio 1) is the output size itself.
inline bool RenderSize(uint32_t outW, uint32_t outH, double ratio, uint32_t* w, uint32_t* h) {
    if (ratio <= 1.0) {
        *w = outW;
        *h = outH;
        return true;
    }
    if (!outW || !outH) return false;
    const double aspect = double(outW) / double(outH);
    for (uint32_t hh = uint32_t(double(outH) / ratio) / 8 * 8; hh >= 720; hh -= 8) {
        const uint32_t ww = uint32_t(std::ceil(double(hh) * aspect / 16.0)) * 16;
        if (ww >= 1280 && ww <= outW && double(ww) / double(hh) <= aspect + 0.009) {
            *w = ww;
            *h = hh;
            return true;
        }
    }
    return false;
}

}  // namespace dlss_modes
