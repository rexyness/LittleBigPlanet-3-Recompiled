// The shared shader cache for the 1440p rescale (common/lbp3_res.h in shadPS4): which shaders get
// a salted cache identity, under which settings, and that the salted names can't collide with the
// original ones. Offline; no GPU.
#include <cstdio>
#include <set>

#include "common/lbp3_res.h"

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
} // namespace

int main() {
    using namespace Lbp3Res;
    const u64 composite = 0x5ed16706fc9e0eb1ull; // listed (the final composite)
    const u64 fill = 0x5c5e1be6e0ada38cull;      // not listed (the masked fill the fills hook keys on)

    // Off (1080p, or 1440p without the shader swap): every hash unchanged.
    CHECK(StoredHash(composite, false, false) == composite);
    CHECK(StoredHash(fill, false, false) == fill);
    // Swap on, shared cache: only the listed shaders are salted.
    CHECK(StoredHash(composite, true, false) == Salt(composite));
    CHECK(StoredHash(composite, true, false) != composite);
    CHECK(StoredHash(fill, true, false) == fill);
    // Swap on, LBP3_RES_CACHE=separate: the old scheme, nothing salted.
    CHECK(StoredHash(composite, true, true) == composite);
    CHECK(StoredHash(fill, true, true) == fill);
    // Separate without the swap: nothing salted either.
    CHECK(StoredHash(composite, false, true) == composite);

    // Every listed shader is recognised in both forms and reports its original hash.
    std::set<u64> originals, salted;
    for (const u64 h : kShaders) {
        u64 o = 0;
        CHECK(IsListed(h, &o) && o == h);
        o = 0;
        CHECK(IsListed(Salt(h), &o) && o == h);
        originals.insert(h);
        salted.insert(Salt(h));
    }
    CHECK(originals.size() == kShaders.size());
    // The list is the one the rescale uses: 44 shaders since the full scan (perf/res1440_shader_scan.md),
    // e.g. an outline/x-ray material variant and tgpshaderblur among them.
    CHECK(kShaders.size() == 44);
    CHECK(IsListedOriginal(0x01bdeb8c981d9707ull));
    CHECK(IsListedOriginal(0xfee8b1e66a624a1eull));
    CHECK(salted.size() == kShaders.size()); // no two listed shaders share a salted name
    // No salted name equals any listed original (a 1080p entry can't be picked up at 1440p).
    for (const u64 s : salted) CHECK(!originals.count(s));
    CHECK(!IsListed(fill));
    CHECK(!IsListedOriginal(Salt(composite)));
    // The salt is a fixed function: cache names stay stable across runs and tools.
    CHECK(Salt(composite) == Salt(0x5ed16706fc9e0eb1ull));
    static_assert(Salt(0x5ed16706fc9e0eb1ull) != 0x5ed16706fc9e0eb1ull);

    std::printf("rescachetest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
