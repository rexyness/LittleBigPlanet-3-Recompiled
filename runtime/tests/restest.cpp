// LBP3_RES (core/res_patches.cpp, shadPS4 common/lbp3_res.h): the eboot edits for each render size
// built from the one 1080p -> size mapping. 2560x1440 must stay byte for byte the original 1440p
// table (main 330a1bc, restest_1440.inc), so the shared shader cache and every 1440p session behave
// as before. Offline; no game, no GPU.
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "common/lbp3_res.h"
#include "core/res_patches.h"

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

struct OldSite {
    const char* group;
    uint64_t offset;
    std::vector<uint8_t> expect, replace;
};
#include "restest_1440.inc"

using core::res::Edit;
using Lbp3Res::Size;

const Edit* Find(const std::vector<Edit>& edits, uint64_t offset) {
    for (const Edit& e : edits)
        if (e.offset == offset) return &e;
    return nullptr;
}
uint32_t Imm32(const std::vector<Edit>& edits, uint64_t offset) {
    const Edit* e = Find(edits, offset);
    if (!e) return 0xffffffffu;
    uint32_t v;
    std::memcpy(&v, e->replace.data() + e->replace.size() - 4, 4);
    return v;
}
uint32_t Imm16(const std::vector<Edit>& edits, uint64_t offset) {
    const Edit* e = Find(edits, offset);
    if (!e) return 0xffffffffu;
    uint16_t v;
    std::memcpy(&v, e->replace.data() + e->replace.size() - 2, 2);
    return v;
}
float ImmF(const std::vector<Edit>& edits, uint64_t offset) {
    const Edit* e = Find(edits, offset);
    if (!e) return -1.0f;
    float f;
    std::memcpy(&f, e->replace.data() + e->replace.size() - 4, 4);
    return f;
}
bool Changed(const std::vector<Edit>& edits, uint64_t offset) {
    const Edit* e = Find(edits, offset);
    return e && e->replace != e->expect;
}

// LBPPC's launcher list (emurphy04, aad6a91, launcher/lbppc_launcher.py RESOLUTIONS).
const Size kList[] = {{1280, 720},  {1920, 1080}, {2560, 1440}, {3840, 2160}, {2560, 1080},
                      {3440, 1440}, {3840, 1600}, {3840, 1080}, {4480, 1260}};

}  // namespace

int main() {
    // ---- Parse ----
    Size s{};
    CHECK(Lbp3Res::Parse("1440", &s) && s == Lbp3Res::k1440);
    CHECK(Lbp3Res::Parse("1080", &s) && s == Lbp3Res::kNative);
    CHECK(Lbp3Res::Parse("2560x1440", &s) && s == Lbp3Res::k1440);
    CHECK(Lbp3Res::Parse("3440X1440", &s) && s.w == 3440 && s.h == 1440);
    CHECK(Lbp3Res::Parse("4480x1260", &s) && s.w == 4480 && s.h == 1260);
    s = {};
    CHECK(!Lbp3Res::Parse("5120x1440", &s) && s == Lbp3Res::kNative);  // wider than the game survives
    CHECK(!Lbp3Res::Parse("1920x1200", &s));                            // narrower than 16:9
    CHECK(!Lbp3Res::Parse("1279x720", &s));
    CHECK(!Lbp3Res::Parse("720", &s));  // only 1080 and 1440 on their own
    CHECK(!Lbp3Res::Parse("2560x", &s));
    CHECK(!Lbp3Res::Parse("x1440", &s));
    CHECK(!Lbp3Res::Parse("2560x1440p", &s));
    CHECK(!Lbp3Res::Parse("", &s) && !Lbp3Res::Parse(nullptr, &s));
    for (const Size& l : kList)
        CHECK(Lbp3Res::Parse((std::to_string(l.w) + "x" + std::to_string(l.h)).c_str(), &s) && s == l);

    // ---- 1080p: nothing ----
    CHECK(core::res::Build(Lbp3Res::kNative).empty());

    // ---- 2560x1440: the original table, byte for byte and in order ----
    const std::vector<Edit> e1440 = core::res::Build(Lbp3Res::k1440);
    constexpr size_t kOldCount = sizeof(kOld1440) / sizeof(kOld1440[0]);
    CHECK(kOldCount == 86);
    CHECK(e1440.size() == kOldCount);
    for (size_t i = 0; i < kOldCount && i < e1440.size(); ++i) {
        const OldSite& o = kOld1440[i];
        const Edit& e = e1440[i];
        const bool same = std::strcmp(o.group, e.group) == 0 && o.offset == e.offset && o.expect == e.expect &&
                          o.replace == e.replace;
        if (!same)
            std::printf("  1440p differs at eboot.bin+0x%llx (%s)\n", static_cast<unsigned long long>(o.offset),
                        o.group);
        CHECK(same);
    }
    CHECK(!core::res::Wide(Lbp3Res::k1440));
    CHECK(core::res::CanvasWidth(Lbp3Res::k1440) == 1280);
    // LBP3_RES_SKIP still drops whole groups.
    const auto noBloom = core::res::Build(Lbp3Res::k1440, {"bloom", false});
    CHECK(noBloom.size() == kOldCount - 6 && !Find(noBloom, 0x427d1c));
    const auto noTwo = core::res::Build(Lbp3Res::k1440, {"ui,pairs", false});
    CHECK(noTwo.size() == kOldCount - 30 && !Find(noTwo, 0xe8235c) && !Find(noTwo, 0x424beb) &&
          Find(noTwo, 0x427d1c));

    // ---- every size: lengths, untouched opcodes, the site list ----
    for (const Size& l : kList) {
        if (l == Lbp3Res::kNative) continue;
        const auto edits = core::res::Build(l);
        const bool wide = core::res::Wide(l);
        CHECK(edits.size() == kOldCount + (wide ? 8 : 0));
        std::set<uint64_t> seen;
        for (const Edit& e : edits) {
            CHECK(e.expect.size() == e.replace.size());
            CHECK(seen.insert(e.offset).second);
            // Only the immediate (the last 2 bytes of a 66-prefixed word move, else the last 4) differs.
            const size_t imm = e.expect[0] == 0x66 ? 2 : 4;
            CHECK(e.expect.size() >= imm &&
                  std::memcmp(e.expect.data(), e.replace.data(), e.expect.size() - imm) == 0);
        }
        // Display size and pitch follow the size exactly.
        CHECK(Imm32(edits, 0x5bf83) == l.w && Imm32(edits, 0x5bf90) == l.h);
        CHECK(Imm32(edits, 0x5bf9e) == l.w * 4 && Imm32(edits, 0x4271c5) == l.w * 4);
        CHECK(Imm16(edits, 0x424c90) == l.w && Imm16(edits, 0x424c96) == l.h);
        CHECK(Imm32(edits, 0x427ef5) == l.h * 2);  // the stacked target
        CHECK(ImmF(edits, 0xe9fdc8) == float(l.w) && ImmF(edits, 0xe9fdcc) == float(l.h));
        // The smaller targets never shrink.
        CHECK(Imm32(edits, 0x427a66) >= 480 && Imm32(edits, 0x427a6b) >= 270);
        CHECK(Imm32(edits, 0x427d1c) >= 1280 && Imm32(edits, 0x427d21) >= 720);
        CHECK(Imm32(edits, 0x427d61) >= 320 && Imm32(edits, 0x427d66) >= 180);
        // The region holds at least the 1080p block grown with the pixel count.
        const uint32_t region = Imm32(edits, 0x1517d);
        CHECK(region >= 0x2441400);
        CHECK(double(region) >= double(0x2441400) * (double(l.w) * l.h / (1920.0 * 1080.0)));
    }

    // ---- 1280x720: the small targets keep their 1080p size ----
    const auto e720 = core::res::Build({1280, 720});
    CHECK(Imm32(e720, 0x427a13) == 1280 && Imm32(e720, 0x427a18) == 720);
    CHECK(!Changed(e720, 0x427a66) && !Changed(e720, 0x427a6b));  // quarter-size 480x270 kept
    CHECK(!Changed(e720, 0x427d1c) && !Changed(e720, 0x427d3d) && !Changed(e720, 0x427d66));  // bloom kept
    CHECK(Imm32(e720, 0x1517d) == 0x2441400);
    CHECK(Imm32(e720, 0x427ef5) == 1440);

    // ---- 3840x2160 ----
    const auto e4k = core::res::Build({3840, 2160});
    CHECK(Imm32(e4k, 0x427a66) == 960 && Imm32(e4k, 0x427a6b) == 540);
    CHECK(Imm32(e4k, 0x427d1c) == 2560 && Imm32(e4k, 0x427d21) == 1440);
    CHECK(Imm32(e4k, 0x1517d) == ((uint32_t(double(0x2441400) * 4 * 1.06) + 0xfffff) & ~0xfffffu));

    // ---- wider than 16:9 ----
    const Size uw{3440, 1440};
    const auto euw = core::res::Build(uw);
    CHECK(core::res::Wide(uw) && core::res::CanvasWidth(uw) == 1720);
    CHECK(ImmF(euw, 0x3fdbc0) == 3440.0f / 1440.0f);
    CHECK(Imm32(euw, 0x1060a04) == 1720);
    CHECK(ImmF(euw, 0xe675c0) == 860.0f && ImmF(euw, 0xe903b8) == 1720.0f);
    CHECK(Imm32(euw, 0x427a13) == 3440 && Imm32(euw, 0x427a18) == 1440);
    CHECK(Imm32(euw, 0x427a66) == 860 && Imm32(euw, 0x427a6b) == 360);
    // 2560x1080: the heights keep their values (no change), the widths grow.
    const auto e21 = core::res::Build({2560, 1080});
    CHECK(!Changed(e21, 0x427a18) && Changed(e21, 0x427a13) && core::res::CanvasWidth({2560, 1080}) == 1707);
    // 32:9 and 2.4:1.
    CHECK(core::res::CanvasWidth({3840, 1080}) == 2560 && core::res::CanvasWidth({4480, 1260}) == 2560);
    CHECK(core::res::CanvasWidth({3840, 1600}) == 1728);
    // The online split: the camera's own aspect stays, the rest widens.
    const auto split = core::res::Build(uw, {"", true});
    CHECK(!Find(split, 0x3fdbc0) && Find(split, 0x1060a04) && split.size() == euw.size() - 1);
    // LBP3_RES_SKIP=aspect: a stretched picture, as before the aspect sites existed.
    CHECK(!Find(core::res::Build(uw, {"aspect", false}), 0x1060a04));
    // 16:9 sizes never touch the aspect sites.
    CHECK(!Find(e4k, 0x3fdbc0) && !Find(e720, 0x1060a04));

    // ---- shader literals (the same mapping) ----
    using Lbp3Res::LiteralSize;
    for (const uint32_t v : {1920u, 1080u, 960u, 540u, 480u, 270u, 3840u, 2160u, 7680u, 4320u})
        CHECK(LiteralSize(v, Lbp3Res::k1440) * 3 == v * 4);  // 4/3 at 1440p, as before
    CHECK(LiteralSize(480, {1280, 720}) == 480 && LiteralSize(270, {1280, 720}) == 270);  // like the target
    CHECK(LiteralSize(1920, {1280, 720}) == 1280 && LiteralSize(540, {1280, 720}) == 360);
    CHECK(LiteralSize(1920, uw) == 3440 && LiteralSize(1080, uw) == 1440 && LiteralSize(960, uw) == 1720);
    CHECK(LiteralSize(480, uw) == 860 && LiteralSize(270, uw) == 360 && LiteralSize(2160, uw) == 2880);
    CHECK(LiteralSize(1234, uw) == 0);
    // The literals and the targets they describe agree at every size.
    for (const Size& l : kList) {
        if (l == Lbp3Res::kNative) continue;
        const auto edits = core::res::Build(l);
        CHECK(Imm32(edits, 0x427a66) == LiteralSize(480, l));
        CHECK(Imm32(edits, 0x427a6b) == LiteralSize(270, l));
        CHECK(Imm32(edits, 0x427a13) == LiteralSize(1920, l));
        CHECK(Imm32(edits, 0x427ef5) == LiteralSize(2160, l));
    }

    // ---- cache salt: 1440p's is the original one; every size has its own ----
    const uint64_t composite = 0x5ed16706fc9e0eb1ull;
    CHECK(Lbp3Res::Salt(composite, Lbp3Res::k1440) == Lbp3Res::Salt(composite));
    CHECK(Lbp3Res::SaltKey(Lbp3Res::k1440) == 0x5245533134343000ull);
    std::set<uint64_t> salts;
    for (const Size& l : kList) salts.insert(Lbp3Res::Salt(composite, l));
    CHECK(salts.size() == sizeof(kList) / sizeof(kList[0]));
    for (const Size& l : kList)
        for (const uint64_t h : Lbp3Res::kShaders) CHECK(!Lbp3Res::IsListedOriginal(Lbp3Res::Salt(h, l)));
    uint64_t original = 0;
    CHECK(Lbp3Res::IsListed(Lbp3Res::Salt(composite, uw), &original, uw) && original == composite);
    CHECK(!Lbp3Res::IsListed(Lbp3Res::Salt(composite, uw), nullptr, Lbp3Res::k1440));
    CHECK(Lbp3Res::StoredHash(composite, true, false, uw) == Lbp3Res::Salt(composite, uw));
    CHECK(Lbp3Res::StoredHash(composite, true, false) == Lbp3Res::Salt(composite));  // 1440 by default

    std::printf("restest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
