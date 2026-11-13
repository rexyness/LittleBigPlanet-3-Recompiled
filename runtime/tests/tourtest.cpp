// LBP3_WARMUP_TOUR (perf/warmup_step.md): the scripted movement while the tour dwells in a level
// (run right, then left, then stand; jump on a timer) and in a cutscene level (an occasional Cross
// tap only), and that nothing is injected while the tour is off.
//   tourtest
#include <cstdio>

#include "hle/pad_types.h"
#include "hle/warmup_tour.h"

using namespace hle::warmup_tour;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failed;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

int main() {
    // A level: right for 5 s, left for 3 s, still for 2 s, repeating every 10 s.
    CHECK(Pattern(0, false).stickX == 1.f);
    CHECK(Pattern(4999, false).stickX == 1.f);
    CHECK(Pattern(5000, false).stickX == -1.f);
    CHECK(Pattern(7999, false).stickX == -1.f);
    CHECK(Pattern(8000, false).stickX == 0.f);
    CHECK(Pattern(9999, false).stickX == 0.f);
    CHECK(Pattern(10000, false).stickX == 1.f);
    CHECK(Pattern(150000, false).stickX == 1.f);
    // Jumps: Cross held for 180 ms out of every 1.1 s, and released in between (so each one registers).
    int pressed = 0, edges = 0;
    bool was = false;
    for (uint64_t ms = 0; ms < 11000; ++ms) {
        const bool c = Pattern(ms, false).cross;
        pressed += c;
        edges += c && !was;
        was = c;
    }
    CHECK(edges == 10);
    CHECK(pressed == 10 * 180);
    // A cutscene: no stick, a short Cross tap every 4 s.
    for (uint64_t ms = 0; ms < 20000; ms += 37) CHECK(Pattern(ms, true).stickX == 0.f);
    CHECK(Pattern(0, true).cross);
    CHECK(!Pattern(200, true).cross);
    CHECK(Pattern(4000, true).cross);
    CHECK(!Pattern(3999, true).cross);
    // The tour is off unless LBP3_WARMUP_TOUR is set (it isn't here): nothing is injected.
    ps4::PadData pad{};
    pad.leftStick = {128, 128};
    ApplyInput(pad);
    CHECK(pad.buttons == 0);
    CHECK(pad.leftStick.x == 128 && pad.leftStick.y == 128);

    std::printf("tourtest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
