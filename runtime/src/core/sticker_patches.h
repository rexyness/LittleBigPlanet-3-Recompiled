// LBP3_STICKERS: sharper stickers (on by default; LBP3_STICKERS=ps4 keeps the PS4's).
//
// LBP3 draws placed stickers into the sticker atlas: a 4096x4096 RGBA8 render target (slot 27 of
// the render-target table, created at eboot.bin+0x427971) cut into a 16x16 grid of 256-px tiles,
// one tile per 250 world units at full sticker density (CWLib TileTex.cpp). These ten same-length
// edits make it 8192x8192 with the grid kept, so each tile is 512 px: twice the texels per axis for
// every sticker. The page table and the material shaders work in tiles (tc0biasscale = the page
// rect / 256; the 1/16 per tile is in the shader), so nothing else changes. It costs 192 MB more
// graphics memory. Measured on the prototype (prototype/sticker-atlas, .scratch/sticker-atlas/
// spike.md): about +10% sticker detail at full sticker density, +24% when the density is cut; a
// sticker blown up larger than its own image stays as soft as that image.
//
// Pure apart from Apply: Edits/Wanted/ApplyTo touch only what they are given (runtime/tests/
// stickertest.cpp).
#pragma once

#include <cstdint>
#include <vector>

namespace core {
class Module;
}

namespace core::stickers {

struct Edit {
    uint64_t offset;               // eboot.bin+offset
    std::vector<uint8_t> expect;   // the original bytes, checked first
    std::vector<uint8_t> replace;  // same length
    const char* what;
};

// The ten edits, in address order.
const std::vector<Edit>& Edits();

// LBP3_STICKERS's value. True (sharper): unset, empty, "on", "1" or "512". False (the PS4's
// atlas): "ps4", "off" or "0", in any case. Anything else counts as on and sets `unknown`.
bool Wanted(const char* value, bool* unknown = nullptr);

// Checks every site of the eboot image at `base` (module offsets) and then changes them all, or
// none if any site differs: half the edits would draw tiles outside the atlas or waste it. Returns
// 0 when applied, else the first site that differs.
uint64_t ApplyTo(uint8_t* base);

// eboot.bin, after relocation: reads LBP3_STICKERS and logs one [stickers] line.
void Apply(Module& m);

}  // namespace core::stickers
