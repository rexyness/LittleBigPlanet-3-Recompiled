// LBP3_RES: render at another size than the PS4's 1920x1080 (perf/res1440.md, perf/resolutions.md).
// The game decides its resolution in a few places, all fixed at 1920x1080: one global (the display
// size, set once in FUN_0005bed0), the render-target declarations (FUN_00427940), two functions
// that build their own colour+depth pair (DrawOffscreenComposite 0x424790 and the SMAA passes
// 0x426dc0) and four float pairs. Every site's original instruction is checked first; if any
// enabled site differs, none is patched. Groups can be left out with LBP3_RES_SKIP=<group,...>
// (core, targets, pairs, ui, bloom, aspect); LBP3_RES_POOL_MB=<MB> also resizes the game's Garlic
// pool (0x6a200000 = 1698 MB). Generated from analysis\eboot.elf and Ghidra listings; the
// stencil-op calls in the same functions that pass 0x1e00 (GL_KEEP, not a pitch) are deliberately
// not in the list.
//
// Each site names the 1080p value its immediate holds and how it follows the size (the mapping is
// shadPS4's common/lbp3_res.h, shared with the shader-literal rescale): full-screen widths and
// heights scale; the smaller targets grow but never shrink; pitches are 4 bytes a pixel; the
// region grows with the pixel count. At 2560x1440 the edits are byte for byte the original 1440p
// table (runtime/tests/restest.cpp).
//
// Wider than 16:9 (2560x1080, 3440x1440, 3840x1600, 3840x1080, 4480x1260): the camera's aspect
// and the 2D layer's canvas widen (the view widens instead of stretching), and the layout's
// built-in screen centres move with the canvas. Online, the camera keeps 16:9 for the simulation
// (players' worlds must step alike) and only its projection reads the wide aspect. The addresses
// of the aspect, canvas, centre and projection sites, the bloom chain, the never-shrink rule and
// the 4480 width limit were found by LBPPC (emurphy04, aad6a91, src/probe.c render_res_install);
// only the addresses and facts are taken from there, the code is ours.
#include "core/res_patches.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/module.h"
#include "hle/online.h"

namespace core::res {
namespace {

using Lbp3Res::Size;

enum class Kind {
    W,       // a full-screen width (1920, or a multiple)
    H,       // a full-screen height (1080, 2160)
    SmallW,  // a smaller target's width: grows, never shrinks
    SmallH,
    BloomW,  // a bloom/capture chain width: widens only with the aspect, a multiple of 16 (BloomWidth)
    BloomH,  // a bloom/capture chain height: always the 1080p one
    Pitch,   // 1920 * 4 bytes
    Region,  // the MainRenderTargets block (0x2441400 bytes)
    FloatW,  // 1920.0f
    FloatH,  // 1080.0f
    Aspect,  // the camera's 16:9 (float)
    Canvas,  // the 2D canvas width, 1280 (int32)
    Centre,  // a 2D layout constant on the 1280-wide canvas (float)
};

struct Site {
    const char* group;
    uint64_t offset;
    std::vector<uint8_t> expect;
    Kind kind;
    uint32_t v1080;  // the value at 1080p (unused for Pitch, Region, Aspect)
    uint8_t immBytes; // the immediate is the instruction's last immBytes bytes (2 or 4)
    const char* what;
};

const std::vector<Site>& Sites() {
    static const std::vector<Site> kSites = {
        // ---- core (3) ----
        {"core", 0x5bf83, {0xc7, 0x03, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV dword ptr [RBX],0x780  (display width)"},
        {"core", 0x5bf90, {0x41, 0xc7, 0x07, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV dword ptr [R15],0x438  (display height)"},
        {"core", 0x5bf9e, {0xc7, 0x00, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV dword ptr [RAX],0x1e00  (display pitch in bytes)"},
        // ---- targets (47) ----
        {"targets", 0x427a13, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427a18, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427a3e, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427a43, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427a66, {0xbe, 0xe0, 0x01, 0x00, 0x00}, Kind::SmallW, 480, 4, "MOV ESI,0x1e0  (quarter-size target width)"},
        // The "MainRenderTargets" pool (bloom chain + the two stacked targets, 60.9 MB at 1440p) comes
        // from a fixed block in the MainHeap layout (FUN_000150f0's table; FUN_00015090 _Exit(1)s
        // when the pool is bigger): 0x2441400 (36.25 MB, a 1080p fit). The MainHeap mapping is
        // summed from that table, so it grows by the same amount.
        {"targets", 0x1517d, {0xc7, 0x43, 0x58, 0x00, 0x14, 0x44, 0x02}, Kind::Region, 0, 4, "MOV dword ptr [RBX + 0x58],0x2441400  (MainRenderTargets block capacity)"},
        {"targets", 0x427a6b, {0xba, 0x0e, 0x01, 0x00, 0x00}, Kind::SmallH, 270, 4, "MOV EDX,0x10e  (quarter-size target height)"},
        {"targets", 0x427a8e, {0xbe, 0xe0, 0x01, 0x00, 0x00}, Kind::SmallW, 480, 4, "MOV ESI,0x1e0  (quarter-size target width)"},
        {"targets", 0x427a93, {0xba, 0x0e, 0x01, 0x00, 0x00}, Kind::SmallH, 270, 4, "MOV EDX,0x10e  (quarter-size target height)"},
        {"targets", 0x427b82, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427b87, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427baa, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427baf, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427bd2, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427bd7, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427bfd, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427c02, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427c28, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427c2d, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427c50, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427c55, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427c78, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427c7d, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427ca0, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427ca5, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427d8c, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427d91, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427db4, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427db9, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427e90, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427e95, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427ebb, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427ec0, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427ef0, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427ef5, {0xba, 0x70, 0x08, 0x00, 0x00}, Kind::H, 2160, 4, "MOV EDX,0x870  (stacked target height (2 x 1080))"},
        {"targets", 0x427f18, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427f1d, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427f4a, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427f4f, {0xba, 0x70, 0x08, 0x00, 0x00}, Kind::H, 2160, 4, "MOV EDX,0x870  (stacked target height (2 x 1080))"},
        {"targets", 0x427f72, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427f77, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427fcd, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427fd2, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x427ff5, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x427ffa, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        {"targets", 0x42809e, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (target width)"},
        {"targets", 0x4280a3, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (target height)"},
        // ---- pairs (26) ----
        {"pairs", 0x424beb, {0xc7, 0x85, 0x40, 0xfe, 0xff, 0xff, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV dword ptr [RBP + -0x1c0],0x438  (DrawOffscreenComposite height (size = pitch * height))"},
        {"pairs", 0x424bf5, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (DrawOffscreenComposite colour target width)"},
        {"pairs", 0x424bfa, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (DrawOffscreenComposite colour target height)"},
        {"pairs", 0x424c45, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (DrawOffscreenComposite depth target width)"},
        {"pairs", 0x424c4a, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (DrawOffscreenComposite depth target height)"},
        {"pairs", 0x424c65, {0xc7, 0x45, 0x94, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV dword ptr [RBP + -0x6c],0x1e00  (DrawOffscreenComposite pitch)"},
        {"pairs", 0x424c89, {0xc7, 0x45, 0xbc, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV dword ptr [RBP + -0x44],0x1e00  (DrawOffscreenComposite pitch)"},
        {"pairs", 0x424c90, {0x66, 0xc7, 0x45, 0xc0, 0x80, 0x07}, Kind::W, 1920, 2, "MOV word ptr [RBP + -0x40],0x780  (DrawOffscreenComposite width)"},
        {"pairs", 0x424c96, {0x66, 0xc7, 0x45, 0xc2, 0x38, 0x04}, Kind::H, 1080, 2, "MOV word ptr [RBP + -0x3e],0x438  (DrawOffscreenComposite height)"},
        {"pairs", 0x424cd9, {0x66, 0xc7, 0x85, 0x80, 0xfe, 0xff, 0xff, 0x80, 0x07}, Kind::W, 1920, 2, "MOV word ptr [RBP + -0x180],0x780  (DrawOffscreenComposite width)"},
        {"pairs", 0x424ce2, {0x66, 0xc7, 0x85, 0x82, 0xfe, 0xff, 0xff, 0x38, 0x04}, Kind::H, 1080, 2, "MOV word ptr [RBP + -0x17e],0x438  (DrawOffscreenComposite height)"},
        {"pairs", 0x424d3f, {0x41, 0xbc, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV R12D,0x1e00  (DrawOffscreenComposite pitch)"},
        {"pairs", 0x424d45, {0x41, 0xbd, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV R13D,0x1e00  (DrawOffscreenComposite pitch)"},
        {"pairs", 0x427049, {0x66, 0xc7, 0x45, 0xc0, 0x80, 0x07}, Kind::W, 1920, 2, "MOV word ptr [RBP + -0x40],0x780  (SMAA pass width)"},
        {"pairs", 0x42704f, {0x66, 0xc7, 0x45, 0xc2, 0x38, 0x04}, Kind::H, 1080, 2, "MOV word ptr [RBP + -0x3e],0x438  (SMAA pass height)"},
        {"pairs", 0x4270ac, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (SMAA pass colour target width)"},
        {"pairs", 0x4270b1, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (SMAA pass colour target height)"},
        {"pairs", 0x427119, {0xbe, 0x80, 0x07, 0x00, 0x00}, Kind::W, 1920, 4, "MOV ESI,0x780  (SMAA pass depth target width)"},
        {"pairs", 0x42711e, {0xba, 0x38, 0x04, 0x00, 0x00}, Kind::H, 1080, 4, "MOV EDX,0x438  (SMAA pass depth target height)"},
        {"pairs", 0x427139, {0xc7, 0x45, 0x94, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV dword ptr [RBP + -0x6c],0x1e00  (SMAA pass pitch)"},
        {"pairs", 0x427150, {0xc7, 0x45, 0xbc, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV dword ptr [RBP + -0x44],0x1e00  (SMAA pass pitch)"},
        {"pairs", 0x4271b3, {0x66, 0xc7, 0x85, 0x58, 0xfe, 0xff, 0xff, 0x80, 0x07}, Kind::W, 1920, 2, "MOV word ptr [RBP + -0x1a8],0x780  (SMAA pass width)"},
        {"pairs", 0x4271bc, {0x66, 0xc7, 0x85, 0x5a, 0xfe, 0xff, 0xff, 0x38, 0x04}, Kind::H, 1080, 2, "MOV word ptr [RBP + -0x1a6],0x438  (SMAA pass height)"},
        {"pairs", 0x4271c5, {0xc7, 0x85, 0x60, 0xfe, 0xff, 0xff, 0x00, 0x1e, 0x00, 0x00}, Kind::Pitch, 0, 4, "MOV dword ptr [RBP + -0x1a0],0x1e00  (SMAA pass pitch)"},
        {"pairs", 0xe811f4, {0x00, 0x00, 0xf0, 0x44}, Kind::FloatW, 1920, 4, "float 1920 (FUN_00424790 distortion texel scale x)"},
        {"pairs", 0xe811fc, {0x00, 0x00, 0x87, 0x44}, Kind::FloatH, 1080, 4, "float 1080 (FUN_00424790 distortion texel scale y)"},
        // ---- ui (4) ----
        {"ui", 0xe8235c, {0x00, 0x00, 0xf0, 0x44}, Kind::FloatW, 1920, 4, "float 1920 (FUN_00433880 snap grid x)"},
        {"ui", 0xe82360, {0x00, 0x00, 0x87, 0x44}, Kind::FloatH, 1080, 4, "float 1080 (FUN_00433880 snap grid y)"},
        {"ui", 0xe9fdc8, {0x00, 0x00, 0xf0, 0x44}, Kind::FloatW, 1920, 4, "float 1920 (FUN_006cd050 NDC to pixels x)"},
        {"ui", 0xe9fdcc, {0x00, 0x00, 0x87, 0x44}, Kind::FloatH, 1080, 4, "float 1080 (FUN_006cd050 NDC to pixels y)"},
        // ---- bloom (6) ----
        // The bloom chain's three targets in the render-target table (1280x720, 640x360, 320x180 at
        // 1080p), which are also the capture chain (render-target indices 0x26/0x27/0x28): photos,
        // costume and level icons and the profile picture are drawn into them and copied out by
        // the CPU. Their sizes follow BloomWidth (below): the 1080p sizes at every 16:9 size, wider
        // only on a wide screen. The sites are the ones LBPPC (emurphy04, aad6a91, src/probe.c
        // targets[]) scales; only the addresses are taken from there, the table entries are ours.
        {"bloom", 0x427d1c, {0xbe, 0x00, 0x05, 0x00, 0x00}, Kind::BloomW, 1280, 4, "MOV ESI,0x500  (bloom target width)"},
        {"bloom", 0x427d21, {0xba, 0xd0, 0x02, 0x00, 0x00}, Kind::BloomH, 720, 4, "MOV EDX,0x2d0  (bloom target height)"},
        {"bloom", 0x427d3d, {0xbe, 0x80, 0x02, 0x00, 0x00}, Kind::BloomW, 640, 4, "MOV ESI,0x280  (bloom target width)"},
        {"bloom", 0x427d42, {0xba, 0x68, 0x01, 0x00, 0x00}, Kind::BloomH, 360, 4, "MOV EDX,0x168  (bloom target height)"},
        {"bloom", 0x427d61, {0xbe, 0x40, 0x01, 0x00, 0x00}, Kind::BloomW, 320, 4, "MOV ESI,0x140  (bloom target width)"},
        {"bloom", 0x427d66, {0xba, 0xb4, 0x00, 0x00, 0x00}, Kind::BloomH, 180, 4, "MOV EDX,0xb4  (bloom target height)"},
        // ---- aspect (8; only wider than 16:9) ----
        // The camera's constructor (0x3fdba0) stores its aspect, 16:9, beside its vertical field of
        // view: at the screen's aspect the view widens instead of stretching; the menus and HUD
        // lay themselves out by it.
        {"aspect", 0x3fdbc0, {0xc7, 0x43, 0x08, 0x39, 0x8e, 0xe3, 0x3f}, Kind::Aspect, 0, 4, "MOV dword ptr [RBX + 0x8],0x3fe38e39  (camera aspect 16:9)"},
        // The 2D layer's canvas (menus, button prompts, subtitles), 1280x720: its projection
        // (0x40ff8b) and the layout read it, so it widens to the screen's aspect.
        {"aspect", 0x1060a04, {0x00, 0x05, 0x00, 0x00}, Kind::Canvas, 1280, 4, "int 1280  (2D canvas width)"},
        // Layout that has the 1280-wide canvas's centre (640) or right edge (1280) built in: the
        // loading screen's text, button prompts.
        {"aspect", 0xe675c0, {0x00, 0x00, 0x20, 0x44}, Kind::Centre, 640, 4, "float 640  (screen centre, 0x27eca0)"},
        {"aspect", 0xe8f668, {0x00, 0x00, 0x20, 0x44}, Kind::Centre, 640, 4, "float 640  (1200-wide text box centre, 0x50879d)"},
        {"aspect", 0xe90394, {0x00, 0x00, 0x20, 0x44}, Kind::Centre, 640, 4, "float 640  (centre, 0x513298 and others)"},
        {"aspect", 0xe903b8, {0x00, 0x00, 0xa0, 0x44}, Kind::Centre, 1280, 4, "float 1280  (right edge, 0x513831)"},
        {"aspect", 0xe90448, {0x00, 0x00, 0x20, 0x44}, Kind::Centre, 640, 4, "float 640  (centre, 0x5192c1)"},
        {"aspect", 0xe9046c, {0x00, 0x00, 0x20, 0x44}, Kind::Centre, 640, 4, "float 640  (centre, 0x5197a1)"},
    };
    return kSites;
}

bool InList(const std::string& list, const char* group) {
    const size_t n = std::strlen(group);
    for (size_t p = 0; (p = list.find(group, p)) != std::string::npos; p += n) {
        const bool startOk = p == 0 || list[p - 1] == ',';
        const bool endOk = p + n == list.size() || list[p + n] == ',';
        if (startOk && endOk) return true;
    }
    return false;
}

uint32_t FloatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// The MainRenderTargets block: 68 MB at 2560x1440 (as it always was there), else the 1080p
// capacity grown with the pixel count plus 6% (a margin from when the bloom chain grew with the
// size too; it now grows less), rounded up to 1 MB, never smaller than at 1080p.
uint32_t RegionBytes(Size s) {
    constexpr uint64_t k1080 = 0x2441400;
    if (s == Lbp3Res::k1440) return 0x4400000;
    const double area = double(s.w) * s.h / (1920.0 * 1080.0);
    if (area <= 1.0) return uint32_t(k1080);
    const uint64_t grown = (uint64_t(double(k1080) * area * 1.06) + 0xfffff) & ~uint64_t{0xfffff};
    return uint32_t(std::max<uint64_t>(grown, k1080));
}

// A bloom/capture chain width (1280, 640 or 320 at 1080p). The CPU copies captures out of the
// chain four frames after drawing them (0x4014f0: a centred photo- or icon-sized cut, rows read
// with a stride of the table's width * 4), which sets two rules:
//  - Widths are multiples of 16 pixels. The GPU lays a linear 32-bit target out with its rows
//    padded to 64 bytes, so the chain scaled to 1440p (1707x960, 853x480, 427x240; c3242e5) was
//    read 853 pixels a row out of 864-pixel rows: every photo, costume icon and profile picture
//    came out sheared into horizontal streaks.
//  - Heights stay 720/360/180 and widths grow only with the aspect. The capture fills the whole
//    target, so a bigger chain made the centred 640x360 photo cut the middle 3/4 of the frame. A
//    wide screen's photos are wider (360 * the aspect, up to 1280x360), so there the chain widens.
// At every 16:9 size that is the PS4's chain; the scaled one looked no different in game
// (perf/best_gfx.md), and LBP3_RES_SKIP=bloom changes nothing at 16:9 now.
uint32_t BloomWidth(uint32_t v1080, Size s) {
    if (!Wide(s)) return v1080;
    // v1080 * (w / h) / (16 / 9), rounded up, then up to a multiple of 16.
    const uint64_t w = (uint64_t(v1080) * 9 * s.w + 16ull * s.h - 1) / (16ull * s.h);
    return uint32_t(std::max<uint64_t>((w + 15) & ~uint64_t{15}, v1080));
}

}  // namespace

bool Wide(Size size) {
    return double(size.w) / double(size.h) > 16.0 / 9.0 + 0.01;
}

int32_t CanvasWidth(Size size) {
    if (!Wide(size)) return 1280;
    return int32_t((720ull * size.w + size.h / 2) / size.h);
}

std::vector<Edit> Build(Size size, const Options& options) {
    std::vector<Edit> out;
    if (size == Lbp3Res::kNative) return out;
    const bool wide = Wide(size);
    const int32_t canvas = CanvasWidth(size);
    for (const Site& s : Sites()) {
        if (InList(options.skip, s.group)) continue;
        if (s.kind == Kind::Aspect || s.kind == Kind::Canvas || s.kind == Kind::Centre) {
            if (!wide) continue;
            if (s.kind == Kind::Aspect && options.aspectSplit) continue;
        }
        uint32_t v = 0;
        switch (s.kind) {
        case Kind::W: v = Lbp3Res::ScaleW(s.v1080, size); break;
        case Kind::H: v = Lbp3Res::ScaleH(s.v1080, size); break;
        case Kind::SmallW: v = Lbp3Res::SmallW(s.v1080, size); break;
        case Kind::SmallH: v = Lbp3Res::SmallH(s.v1080, size); break;
        case Kind::BloomW: v = BloomWidth(s.v1080, size); break;
        case Kind::BloomH: v = s.v1080; break;
        case Kind::Pitch: v = size.w * 4; break;
        case Kind::Region: v = RegionBytes(size); break;
        case Kind::FloatW: v = FloatBits(float(Lbp3Res::ScaleW(s.v1080, size))); break;
        case Kind::FloatH: v = FloatBits(float(Lbp3Res::ScaleH(s.v1080, size))); break;
        case Kind::Aspect: v = FloatBits(float(size.w) / float(size.h)); break;
        case Kind::Canvas: v = uint32_t(canvas); break;
        case Kind::Centre: v = FloatBits(float(s.v1080) * float(canvas) / 1280.0f); break;
        }
        Edit e{s.group, s.offset, s.expect, s.expect, s.what};
        uint8_t* imm = e.replace.data() + e.replace.size() - s.immBytes;
        if (s.immBytes == 2) {
            const uint16_t v16 = uint16_t(v);
            std::memcpy(imm, &v16, 2);
        } else {
            std::memcpy(imm, &v, 4);
        }
        char buf[64];
        if (s.kind == Kind::FloatW || s.kind == Kind::FloatH || s.kind == Kind::Aspect || s.kind == Kind::Centre) {
            float f;
            std::memcpy(&f, &v, 4);
            std::snprintf(buf, sizeof(buf), " -> %g", double(f));
        } else {
            std::snprintf(buf, sizeof(buf), " -> 0x%x (%u)", v, v);
        }
        e.what += buf;
        out.push_back(std::move(e));
    }
    return out;
}

namespace {

// Executable memory within reach of a rel32 jump from the image.
uint8_t* AllocNear(const uint8_t* anchor, size_t size) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t start = reinterpret_cast<uintptr_t>(anchor) & ~(gran - 1);
    for (uintptr_t d = gran; d < 0x70000000u; d += gran) {
        for (const uintptr_t at : {start - d, start + d}) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE | MEM_COMMIT,
                                       PAGE_EXECUTE_READWRITE))
                return static_cast<uint8_t*>(p);
        }
    }
    return nullptr;
}

bool Rel32(const uint8_t* from, const uint8_t* to, int32_t* out) {
    const int64_t d = to - from;
    if (d != int64_t(int32_t(d))) return false;
    *out = int32_t(d);
    return true;
}

// The aspect split: the projection's "vmovss xmm2,[rbx+8]; vucomiss xmm2,xmm1" (9 bytes) becomes
// "jmp stub; nop4", and the stub loads the screen's aspect instead, redoes the comparison and jumps
// back to the "jbe" after it. No stack is used (the jump leaves the flags for the jbe). The
// camera's own aspect, which the simulation reads, stays 16:9.
bool InstallSplit(Module& m, Size size) {
    uint8_t* site = m.Addr(kSplitSite);
    uint8_t* stub = AllocNear(site, 4096);
    if (!stub) return false;
    float* aspect = reinterpret_cast<float*>(stub + 32);
    *aspect = float(size.w) / float(size.h);
    uint8_t* o = stub;
    int32_t rel = 0;
    *o++ = 0xc5; *o++ = 0xfa; *o++ = 0x10; *o++ = 0x15;  // vmovss xmm2, [rip + aspect]
    if (!Rel32(o + 4, reinterpret_cast<uint8_t*>(aspect), &rel)) return false;
    std::memcpy(o, &rel, 4);
    o += 4;
    *o++ = 0xc5; *o++ = 0xf8; *o++ = 0x2e; *o++ = 0xd1;  // vucomiss xmm2, xmm1
    *o++ = 0xe9;                                         // jmp back to the jbe
    if (!Rel32(o + 4, m.Addr(kSplitResume), &rel)) return false;
    std::memcpy(o, &rel, 4);
    o += 4;
    uint8_t jmp[9] = {0xe9, 0, 0, 0, 0, 0x0f, 0x1f, 0x40, 0x00};  // jmp stub; nop dword [rax]
    if (!Rel32(site + 5, stub, &rel)) return false;
    std::memcpy(jmp + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), stub, 64);
    std::memcpy(site, jmp, sizeof(jmp));
    return true;
}

bool EnvOn(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] == '1';
}

}  // namespace

void Apply(Module& m) {
    const char* raw = std::getenv("LBP3_RES");
    const Size size = Lbp3Res::Current();
    if (raw && *raw && size == Lbp3Res::kNative && std::strcmp(raw, "1080") != 0 && std::strcmp(raw, "1920x1080") != 0)
        std::fprintf(stderr,
                     "[res] LBP3_RES=%s is not a usable size (1280x720 up to %u wide, 16:9 or wider): the game "
                     "stays at 1920x1080\n",
                     raw, Lbp3Res::kMaxWidth);
    if (size == Lbp3Res::kNative) return;

    Options options;
    if (const char* skip = std::getenv("LBP3_RES_SKIP")) options.skip = skip;
    const bool wide = Wide(size) && !InList(options.skip, "aspect");
    // Online (the levels server or shadNet), the simulation's camera keeps 16:9: players' worlds
    // must step alike (LBPPC saw "The game diverged" with one player ultrawide). LBP3_RES_ASPECT_SPLIT
    // =1/0 forces the split on or off.
    const bool online = hle::online::Enabled() || EnvOn("LBP3_SHADNET");
    const char* splitEnv = std::getenv("LBP3_RES_ASPECT_SPLIT");
    options.aspectSplit = wide && (splitEnv && *splitEnv ? splitEnv[0] == '1' : online);
    const std::vector<Edit> edits = Build(size, options);

    // Check every enabled site first: a partial resize would leave mismatched targets.
    int bad = 0;
    for (const Edit& e : edits) {
        if (std::memcmp(m.Addr(e.offset), e.expect.data(), e.expect.size()) != 0) {
            std::fprintf(stderr, "[res] eboot.bin+0x%llx (%s): bytes differ, the %ux%u patch set is NOT applied: %s\n",
                         static_cast<unsigned long long>(e.offset), e.group, size.w, size.h, e.what.c_str());
            ++bad;
        }
    }
    uint8_t* pool = m.Addr(kPoolSite);  // MOV EDX,0x6a200000: the Garlic pool size in FUN_0005bed0
    const uint8_t poolExpect[5] = {0xba, 0x00, 0x00, 0x20, 0x6a};
    const char* poolMb = std::getenv("LBP3_RES_POOL_MB");
    if (poolMb && std::memcmp(pool, poolExpect, 5) != 0) {
        std::fprintf(stderr, "[res] eboot.bin+0x5bf55 (pool): bytes differ, the %ux%u patch set is NOT applied\n",
                     size.w, size.h);
        ++bad;
    }
    if (options.aspectSplit && std::memcmp(m.Addr(kSplitSite), kSplitExpect.data(), kSplitExpect.size()) != 0) {
        std::fprintf(stderr, "[res] eboot.bin+0x%llx (aspect split): bytes differ, the %ux%u patch set is NOT applied\n",
                     static_cast<unsigned long long>(kSplitSite), size.w, size.h);
        ++bad;
    }
    if (bad) return;

    int changed = 0;
    for (const Edit& e : edits) {
        if (e.replace == e.expect) continue;  // e.g. heights at 2560x1080
        std::memcpy(m.Addr(e.offset), e.replace.data(), e.replace.size());
        std::fprintf(stderr, "[res] eboot.bin+0x%llx %s: %s\n", static_cast<unsigned long long>(e.offset), e.group,
                     e.what.c_str());
        ++changed;
    }
    if (poolMb) {
        const uint64_t mb = std::strtoull(poolMb, nullptr, 10);
        const uint32_t bytes = static_cast<uint32_t>(std::min<uint64_t>(((mb + 1) / 2) * 2, 3072) << 20);  // 2 MB aligned
        std::memcpy(pool + 1, &bytes, 4);
        std::fprintf(stderr, "[res] eboot.bin+0x5bf55 pool: Garlic 1698 -> %u MB\n", bytes >> 20);
    }
    const char* aspectNote = "16:9";
    if (wide) {
        aspectNote = "wide view, canvas widened";
        if (options.aspectSplit) {
            if (InstallSplit(m, size)) {
                aspectNote = "wide view drawn, the simulation's camera kept at 16:9 (online)";
                ++changed;
            } else {
                aspectNote = "the view kept at 16:9: no memory near the image for the online split";
            }
        }
    }
    std::fprintf(stderr, "[res] %ux%u: %d sites patched (display %ux%u, aspect %.3f: %s; canvas %dx720; LBP3_RES_SKIP=%s)\n",
                 size.w, size.h, changed, size.w, size.h, double(size.w) / size.h, aspectNote, CanvasWidth(size),
                 options.skip.empty() ? "none" : options.skip.c_str());
}

}  // namespace core::res
