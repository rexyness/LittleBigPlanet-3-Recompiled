// LBP3_STICKERS (core/sticker_patches.cpp): the sticker atlas at 512-px tiles. Pins the ten sites
// and their bytes (checked against the 1.28 eboot, .scratch/sticker-atlas/spike.md), the setting's
// values, and that the edits go in all together or not at all. Offline; no game, no GPU.
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/sticker_patches.h"

namespace {
int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(c)) {                                                                                \
            ++g_failed;                                                                            \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #c);                                   \
        }                                                                                          \
    } while (0)

using core::stickers::Edit;
using Bytes = std::vector<uint8_t>;

struct Site {
    uint64_t offset;
    Bytes expect, replace;
};
// The sticker atlas's size in the render-target builder, and the two RenderDecals tile viewports.
const Site kSites[] = {
    {0x427971, {0xbe, 0x00, 0x10, 0x00, 0x00}, {0xbe, 0x00, 0x20, 0x00, 0x00}},  // mov esi,0x1000 -> 0x2000
    {0x427976, {0xba, 0x00, 0x10, 0x00, 0x00}, {0xba, 0x00, 0x20, 0x00, 0x00}},  // mov edx,0x1000 -> 0x2000
    {0x47ffee, {0xc1, 0xe1, 0x08}, {0xc1, 0xe1, 0x09}},                          // shl ecx,8 -> 9
    {0x47fff7, {0xc1, 0xe7, 0x08}, {0xc1, 0xe7, 0x09}},                          // shl edi,8 -> 9
    {0x47fffd, {0xba, 0x00, 0x01, 0x00, 0x00}, {0xba, 0x00, 0x02, 0x00, 0x00}},  // mov edx,0x100 -> 0x200
    {0x480002, {0xb9, 0x00, 0x01, 0x00, 0x00}, {0xb9, 0x00, 0x02, 0x00, 0x00}},  // mov ecx,0x100 -> 0x200
    {0x480549, {0xc1, 0xe7, 0x08}, {0xc1, 0xe7, 0x09}},                          // shl edi,8 -> 9
    {0x480554, {0xc1, 0xe6, 0x08}, {0xc1, 0xe6, 0x09}},                          // shl esi,8 -> 9
    {0x480557, {0xba, 0x00, 0x01, 0x00, 0x00}, {0xba, 0x00, 0x02, 0x00, 0x00}},  // mov edx,0x100 -> 0x200
    {0x48055c, {0xb9, 0x00, 0x01, 0x00, 0x00}, {0xb9, 0x00, 0x02, 0x00, 0x00}},  // mov ecx,0x100 -> 0x200
};
constexpr size_t kImage = 0x481000;  // covers every site

void TestSites() {
    const std::vector<Edit>& edits = core::stickers::Edits();
    CHECK(edits.size() == std::size(kSites));
    for (size_t i = 0; i < edits.size() && i < std::size(kSites); ++i) {
        CHECK(edits[i].offset == kSites[i].offset);
        CHECK(edits[i].expect == kSites[i].expect);
        CHECK(edits[i].replace == kSites[i].replace);
        CHECK(edits[i].what && *edits[i].what);
    }
}

void TestSetting() {
    using core::stickers::Wanted;
    bool unknown = true;
    CHECK(Wanted(nullptr, &unknown) && !unknown);  // unset: on (the default)
    CHECK(Wanted("", &unknown) && !unknown);
    for (const char* on : {"on", "1", "512", "ON"}) CHECK(Wanted(on, &unknown) && !unknown);
    for (const char* off : {"ps4", "PS4", "off", "0"}) CHECK(!Wanted(off, &unknown) && !unknown);
    CHECK(Wanted("sharp", &unknown) && unknown);  // unknown: stays on, and says so
}

Bytes Original() {
    Bytes image(kImage, 0xcc);
    for (const Site& s : kSites) std::memcpy(image.data() + s.offset, s.expect.data(), s.expect.size());
    return image;
}

void TestAllOrNothing() {
    Bytes image = Original();
    CHECK(core::stickers::ApplyTo(image.data()) == 0);  // all applied
    for (const Site& s : kSites) CHECK(std::memcmp(image.data() + s.offset, s.replace.data(), s.replace.size()) == 0);
    // Applied once already (or another game version at the last site): nothing more changes.
    const Bytes patched = image;
    CHECK(core::stickers::ApplyTo(image.data()) == kSites[0].offset);
    CHECK(image == patched);

    Bytes other = Original();
    other[kSites[9].offset + 1] ^= 0xff;  // one site differs
    const Bytes before = other;
    CHECK(core::stickers::ApplyTo(other.data()) == kSites[9].offset);
    CHECK(other == before);  // not one byte changed
}

}  // namespace

int main() {
    TestSites();
    TestSetting();
    TestAllOrNothing();
    std::printf("stickertest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
