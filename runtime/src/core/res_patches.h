// LBP3_RES: the game renders at another size than the PS4's 1920x1080 (perf/res1440.md,
// perf/resolutions.md). The byte edits are built here from one table of the game's size sites and
// the one 1080p -> size mapping in shadPS4's common/lbp3_res.h (shared with the shader-literal
// rescale and the cache salt). Pure: nothing here touches game memory, so tests can check the
// edits offline (runtime/tests/restest.cpp pins 2560x1440 to the original 1440p table).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/lbp3_res.h"

namespace core {
class Module;
}

namespace core::res {

struct Edit {
    const char* group;
    uint64_t offset;               // eboot.bin+offset
    std::vector<uint8_t> expect;   // the original bytes (checked before anything is changed)
    std::vector<uint8_t> replace;  // same length
    std::string what;
};

struct Options {
    // LBP3_RES_SKIP's groups (core, targets, pairs, ui, bloom, aspect), comma-separated.
    std::string skip;
    // The camera keeps 16:9 for the game's simulation and only the projection gets the screen's
    // aspect (online play: players' worlds must step alike). The camera site is then left out of
    // the edits here; patches.cpp redirects the projection's read instead (SplitSite).
    bool aspectSplit = false;
};

// The edits for `size` (none at 1920x1080).
std::vector<Edit> Build(Lbp3Res::Size size, const Options& options = {});

// The screen's aspect differs enough from 16:9 that the camera and the 2D canvas widen.
bool Wide(Lbp3Res::Size size);

// The 2D layer's canvas width at `size` (1280 unless Wide).
int32_t CanvasWidth(Lbp3Res::Size size);

// The projection's aspect read (aspect split): "vmovss xmm2,[rbx+8]; vucomiss xmm2,xmm1" at
// eboot.bin+0x3fe17f in the camera projection 0x3fe160, then "jbe" at 0x3fe188.
constexpr uint64_t kSplitSite = 0x3fe17f;
constexpr uint64_t kSplitResume = 0x3fe188;
inline const std::vector<uint8_t> kSplitExpect = {0xc5, 0xfa, 0x10, 0x53, 0x08, 0xc5, 0xf8, 0x2e, 0xd1};

// The game's Garlic pool size instruction in FUN_0005bed0 (LBP3_RES_POOL_MB).
constexpr uint64_t kPoolSite = 0x5bf55;

// The Garlic pool for `size` in MB, or 0 to keep the game's 1,698 MB. Above 2560x1440's pixel
// count it's 1,698 MB plus 320 MiB per extra 1920x1080 of pixels, rounded up to 2 MiB
// (sethlikepie's "heap:320" rule, littlebigplanet3-pc 07cc7e7; 2,658 MB at 3840x2160), at most
// 3,072. Up to 1440p the game's pool fits with room (LocalRenderTargets 635 MB at 1440p,
// perf/res1440_experiment.md), and direct memory is committed when mapped, so it's left alone
// there. A margin for the larger sizes, not a measured need: 4K has loaded without it.
// LBP3_RES_POOL_MB overrides it.
uint32_t PoolMb(Lbp3Res::Size size);

// Patches eboot.bin for LBP3_RES (nothing when unset, 1080 or unusable).
void Apply(Module& m);

}  // namespace core::res
