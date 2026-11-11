#include "hle/modal.h"

#include <atomic>
#include <chrono>
#include <cstdio>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hle/pad_types.h"

namespace hle::modal {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kReleaseTimeout = std::chrono::seconds(1);

std::atomic<int> g_open{0};
// Steady-clock ticks until which the gate waits for the answering press to be released
// after the last dialog closed; 0 = not waiting.
std::atomic<int64_t> g_releaseUntil{0};

int64_t Now() { return Clock::now().time_since_epoch().count(); }

}  // namespace

void Begin(const char* who) {
    const int n = ++g_open;
    std::fprintf(stderr, "[modal] %s opened (%d open)\n", who ? who : "?", n);
}

void End() {
    int n = g_open.load();
    while (n > 0 && !g_open.compare_exchange_weak(n, n - 1)) {
    }
    if (n == 1) g_releaseUntil = Now() + std::chrono::duration_cast<Clock::duration>(kReleaseTimeout).count();
    std::fprintf(stderr, "[modal] closed (%d open)\n", n > 0 ? n - 1 : 0);
}

bool Active() { return g_open.load() > 0; }

bool AnswerHeld(uint32_t realButtons) {
    constexpr uint32_t kAnyButton = ps4::PAD_L3 | ps4::PAD_R3 | ps4::PAD_OPTIONS | ps4::PAD_UP | ps4::PAD_RIGHT |
                                    ps4::PAD_DOWN | ps4::PAD_LEFT | ps4::PAD_L2 | ps4::PAD_R2 | ps4::PAD_L1 |
                                    ps4::PAD_R1 | ps4::PAD_TRIANGLE | ps4::PAD_CIRCLE | ps4::PAD_CROSS |
                                    ps4::PAD_SQUARE | ps4::PAD_TOUCH_PAD;
    if (realButtons & kAnyButton) return true;
    // Enter and Esc confirm and cancel the dialogs; once the game window has the focus
    // again they are keyboard actions (Confirm, Back) too, and so is Backspace (Back).
    return ((GetAsyncKeyState(VK_RETURN) | GetAsyncKeyState(VK_ESCAPE) | GetAsyncKeyState(VK_BACK)) & 0x8000) != 0;
}

bool HoldsPad(uint32_t realButtons, int slot) {
    if (g_open.load() > 0) return true;
    if (slot != 0) return false;
    const int64_t until = g_releaseUntil.load();
    if (!until) return false;
    if (AnswerHeld(realButtons) && Now() < until) return true;
    int64_t expected = until;
    g_releaseUntil.compare_exchange_strong(expected, 0);  // a dialog that closed since keeps its own wait
    return false;
}

}  // namespace hle::modal
