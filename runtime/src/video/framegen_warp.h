// Frame generation: the motion-compensated in-between frame (LBP3_FRAMEGEN_MODE=warp;
// perf/fg_warp.md in the planning repo).
//
// It works from the two real frames alone, the way TV frame-rate converters do. On a half
// resolution luma pyramid it searches, for each 8x8 block of the in-between frame, the half-step
// displacement h that makes N-1 at p - h and N at p + h agree (bilateral motion estimation, coarse
// to fine, with neighbour candidates and a smoothness cost). The block field is then interpolated
// per pixel, and the output pixel is the average of the two samples. Where they disagree
// (occlusions, effects with no consistent motion), each pixel tries its neighbouring blocks'
// vectors, and if none agrees it falls back to the nearer real frame's sample. A pixel that is the
// same in N-1 and N is copied as is, which keeps static HUD text sharp over a moving scene.
//
// Optional inputs (GenInputs): the game's own motion vectors and the HUD-less colour. Their
// encoding is still being confirmed (perf/fg_inputs.md), so they are only used when enabled
// (LBP3_FRAMEGEN_WARP_VEC=1 with the decode parameters below), as extra search candidates.
//
// Host copies only (BGRA8 or RGBA8 rows: the luma treats R and B alike), multi-threaded, no GPU.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "video/framegen.h"

namespace video::framegen {

struct WarpParams {
    int threads = 8;            // worker threads (the generator's own pool)
    int coarseRadius = 8;       // full search at 1/8 resolution, in half-step pixels there (8 = +-128 px of motion)
    int refineRadius = 1;       // per finer level
    int smoothPasses = 2;       // neighbour-candidate passes per level
    int lambdaSmooth = 4;       // cost per pixel of distance from the neighbours' median vector (x 64 px per block)
    int lambdaZero = 1;         // tie-break toward no motion
    int agreeT = 24;            // samples within this (max channel) are averaged as they are
    int altT = 48;              // above this after the alternative vectors: use the nearer frame's sample
    int staticT = 2;            // a pixel within this in N-1 and N is copied (static HUD, unchanged areas)
    bool perPixelAlt = true;    // per-pixel choice among the neighbouring blocks' vectors
    bool staticCopy = true;
    int fallback = 1;           // 0 = average anyway, 1 = the frame nearer t, 2 = always N
    // The game's velocity target (RGBA8, perf/fg_inputs.md): the pixel at p in N was at p - m in
    // N-1, m.x = (B - 127) * 0.941, m.y = (G - 127) * 0.529 px; R = 255 where it has a vector.
    // Codes 0 and 255 are saturated (unknown). Used as search candidates.
    bool useGameVectors = true;
    float vecBias = 127.0f, vecScaleX = 0.941f, vecScaleY = 0.529f;
    int vecChanX = 2, vecChanY = 1, vecChanCover = 0;
    float maxSaturated = 0.97f;  // decline when nearly all covered vectors are saturated (a cut: 100%)
    // HUD-less colour: interpolate it, then put back the real frame's HUD (where N differs from its
    // HUD-less image by more than hudT).
    bool useHudless = true;
    int hudT = 8;
    // In-world text drawn into the scene (the map's "Loading..." ring) is white, like the map's
    // white stripes. Where a near-white pixel is the same in both frames within textD pixels of a
    // thin bright stroke (min channel >= textW, and at least textC brighter than the luma within
    // textR pixels on both sides along x or y), and isn't a stroke in both frames itself, it is
    // not taken as static (copied unwarped, which erases letters crossing the stripes) but drawn
    // from the nearer frame, moved with the scene. LBP3_FRAMEGEN_TEXT=off restores the old
    // behaviour.
    bool textKeep = true;
    int textW = 236, textC = 48, textR = 8, textD = 8;
};

// LBP3_FRAMEGEN_WARP_* overrides of the defaults (threads, radius, thresholds, fallback, vectors).
WarpParams WarpParamsFromEnv();

std::unique_ptr<Generator> MakeWarpGenerator(const WarpParams& p);

// For the offline evaluator: the block field of the last Generate (half-step vectors in
// full-resolution pixels, one per 16x16 block) and per-stage times.
struct WarpDebug {
    uint32_t blocksX = 0, blocksY = 0;
    std::vector<float> hx, hy;  // half-step displacement per block (full-res pixels)
    std::vector<uint32_t> cost;
    double msLuma = 0, msSearch = 0, msCompose = 0;
    uint64_t pixAveraged = 0, pixAlt = 0, pixFallback = 0, pixStatic = 0, pixHud = 0, pixText = 0;
    double vecCovered = 0, vecSaturated = 0;  // shares of the frame
    bool usedVectors = false, usedHudless = false, declined = false;
    // Per pixel, when LBP3_FRAMEGEN_WARP_KINDS=1 (fgeval): 0 averaged, 1 averaged after an
    // alternative vector, 2 nearer frame (fallback), 3 static copy, 4 white near text (nearer frame).
    std::vector<uint8_t> kinds;
};
const WarpDebug* LastWarpDebug(const Generator* g);  // nullptr when g isn't a warp generator

}  // namespace video::framegen
