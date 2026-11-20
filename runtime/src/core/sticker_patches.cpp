// LBP3_STICKERS: the sticker atlas at 512-px tiles. See sticker_patches.h.
#include "core/sticker_patches.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/module.h"

namespace core::stickers {

const std::vector<Edit>& Edits() {
    static const std::vector<Edit> kEdits = {
        // The render-target builder (0x427940): the sticker atlas's size, 4096 -> 8192.
        {0x427971, {0xbe, 0x00, 0x10, 0x00, 0x00}, {0xbe, 0x00, 0x20, 0x00, 0x00}, "MOV ESI,0x1000  (sticker atlas width)"},
        {0x427976, {0xba, 0x00, 0x10, 0x00, 0x00}, {0xba, 0x00, 0x20, 0x00, 0x00}, "MOV EDX,0x1000  (sticker atlas height)"},
        // RenderDecals (0x47fb80), redrawing a tile: viewport (x<<8, y<<8, 256, 256) -> 512-px tiles.
        {0x47ffee, {0xc1, 0xe1, 0x08}, {0xc1, 0xe1, 0x09}, "SHL ECX,8  (tile x)"},
        {0x47fff7, {0xc1, 0xe7, 0x08}, {0xc1, 0xe7, 0x09}, "SHL EDI,8  (tile y)"},
        {0x47fffd, {0xba, 0x00, 0x01, 0x00, 0x00}, {0xba, 0x00, 0x02, 0x00, 0x00}, "MOV EDX,0x100  (tile width)"},
        {0x480002, {0xb9, 0x00, 0x01, 0x00, 0x00}, {0xb9, 0x00, 0x02, 0x00, 0x00}, "MOV ECX,0x100  (tile height)"},
        // RenderDecals, a newly allocated tile: the same viewport.
        {0x480549, {0xc1, 0xe7, 0x08}, {0xc1, 0xe7, 0x09}, "SHL EDI,8  (new tile x)"},
        {0x480554, {0xc1, 0xe6, 0x08}, {0xc1, 0xe6, 0x09}, "SHL ESI,8  (new tile y)"},
        {0x480557, {0xba, 0x00, 0x01, 0x00, 0x00}, {0xba, 0x00, 0x02, 0x00, 0x00}, "MOV EDX,0x100  (new tile width)"},
        {0x48055c, {0xb9, 0x00, 0x01, 0x00, 0x00}, {0xb9, 0x00, 0x02, 0x00, 0x00}, "MOV ECX,0x100  (new tile height)"},
    };
    return kEdits;
}

bool Wanted(const char* value, bool* unknown) {
    if (unknown) *unknown = false;
    std::string v = value ? value : "";
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v.empty() || v == "on" || v == "1" || v == "512") return true;
    if (v == "ps4" || v == "off" || v == "0") return false;
    if (unknown) *unknown = true;
    return true;
}

uint64_t ApplyTo(uint8_t* base) {
    for (const Edit& e : Edits())
        if (std::memcmp(base + e.offset, e.expect.data(), e.expect.size()) != 0) return e.offset;
    for (const Edit& e : Edits()) std::memcpy(base + e.offset, e.replace.data(), e.replace.size());
    return 0;
}

void Apply(Module& m) {
    const char* v = std::getenv("LBP3_STICKERS");
    bool unknown = false;
    const bool wanted = Wanted(v, &unknown);
    if (unknown)
        std::fprintf(stderr, "[stickers] LBP3_STICKERS=%s isn't a known value (ps4 or off keeps the PS4's atlas)\n", v);
    if (!wanted) {
        std::fprintf(stderr, "[stickers] LBP3_STICKERS=%s: the PS4's sticker atlas (4096x4096, 256-px tiles)\n", v);
        return;
    }
    if (const uint64_t bad = ApplyTo(m.Addr(0))) {
        std::fprintf(stderr, "[stickers] eboot.bin+0x%llx: bytes differ, the sticker atlas stays the PS4's\n",
                     static_cast<unsigned long long>(bad));
        return;
    }
    std::fprintf(stderr, "[stickers] sharper stickers: the sticker atlas is 8192x8192 (512-px tiles), %zu sites patched"
                         " (LBP3_STICKERS=ps4 keeps the PS4's)\n",
                 Edits().size());
}

}  // namespace core::stickers
