// LBP3_MY_UNLOCKS's import guard (ticket 19): while the import runs, the game's import screen
// must not see its cancel button (Circle on this US build, Cross where the region flag says so);
// every other button passes, and nothing changes when no import runs or the switch is off.
//   unlocktest
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "hle/my_unlocks.h"
#include "hle/pad_types.h"

using namespace hle::my_unlocks;

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
    const uint32_t circle = ps4::PAD_CIRCLE, cross = ps4::PAD_CROSS, options = ps4::PAD_OPTIONS;
    const uint32_t square = ps4::PAD_SQUARE, up = ps4::PAD_UP;

    // No import running: everything passes, whatever the layout.
    for (bool backIsCircle : {true, false}) {
        CHECK(FilterBack(circle, false, backIsCircle) == circle);
        CHECK(FilterBack(cross, false, backIsCircle) == cross);
        CHECK(FilterBack(circle | cross | options, false, backIsCircle) == (circle | cross | options));
        CHECK(FilterBack(0, false, backIsCircle) == 0);
    }

    // Import running, US layout (back = Circle): Circle is held off, Cross (the OK button for the
    // message afterwards) and the rest pass.
    CHECK(FilterBack(circle, true, true) == 0);
    CHECK(FilterBack(cross, true, true) == cross);
    CHECK(FilterBack(circle | cross, true, true) == cross);
    CHECK(FilterBack(circle | options | square | up, true, true) == (options | square | up));
    CHECK(FilterBack(options, true, true) == options);

    // Import running, the other layout (back = Cross).
    CHECK(FilterBack(cross, true, false) == 0);
    CHECK(FilterBack(circle, true, false) == circle);
    CHECK(FilterBack(circle | cross | options, true, false) == (circle | options));

    // The bits are the ones the game's pad reader maps to its cancel bits (FUN_00064f10:
    // PS4 Circle 0x2000 -> 0x20, Cross 0x4000 -> 0x40).
    CHECK(circle == 0x2000);
    CHECK(cross == 0x4000);

    // With no eboot loaded the switch can't install (no module): GuardImport passes everything,
    // even with LBP3_MY_UNLOCKS set.
    _putenv_s("LBP3_MY_UNLOCKS", "lbp1,lbp2");
    InstallHooks();
    CHECK(GuardImport(circle) == circle);
    CHECK(GuardImport(circle | cross | options) == (circle | cross | options));
    CHECK(GuardImport(0) == 0);
    _putenv_s("LBP3_MY_UNLOCKS", "");

    // The in-game menu's state: unavailable without the hook, whatever else is set.
    CHECK(Decide(false, 0x7f, true, true, true) == State::Unavailable);
    CHECK(Decide(true, 0, false, false, false) == State::Idle);
    CHECK(Decide(true, 0x7f, false, false, false) == State::Armed);
    CHECK(Decide(true, 0, false, true, false) == State::Running);   // inside ActivateImportScreen
    CHECK(Decide(true, 0, true, false, true) == State::Running);    // ours is on screen
    CHECK(Decide(true, 0, true, false, false) == State::Done);
    CHECK(Decide(true, 0x7f, true, false, false) == State::Armed);  // armed again after one ran
    // The game's own import screen (not started here) doesn't count as ours.
    CHECK(Decide(true, 0, false, false, true) == State::Idle);
    // No eboot here, so no hook: the menu can't arm it.
    CHECK(GetState() == State::Unavailable);
    CHECK(!Request());
    CHECK(GetState() == State::Unavailable);
    Cancel();
    CHECK(GuardImport(circle) == circle);

    std::printf("unlocktest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
