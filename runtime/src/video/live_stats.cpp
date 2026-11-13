// Live numbers for the in-game menu. See live_stats.h.
#include "video/live_stats.h"

#include <algorithm>
#include <chrono>
#include <mutex>

namespace video::live {
namespace {

using Clock = std::chrono::steady_clock;

struct Window {
    int flips = 0;
    double workSum = 0, intervalSum = 0, worst = 0;
    int shown = 0, generated = 0, latencyN = 0;
    double latencySum = 0;
};

std::mutex g_mutex;
Window g_cur;
Snapshot g_last;
Clock::time_point g_start = Clock::now();

// Closes the window once a second has passed (lock held).
void Roll(Clock::time_point now) {
    const double secs = std::chrono::duration<double>(now - g_start).count();
    if (secs < 1.0) return;
    Snapshot s;
    s.valid = true;
    s.gameFps = g_cur.flips / secs;
    s.frameMs = g_cur.flips ? g_cur.intervalSum / g_cur.flips : 0;
    s.worstFrameMs = g_cur.worst;
    s.workMs = g_cur.flips ? g_cur.workSum / g_cur.flips : 0;
    s.shownFps = g_cur.shown / secs;
    s.generatedShare = g_cur.shown ? double(g_cur.generated) / g_cur.shown : 0;
    s.latencyMs = g_cur.latencyN ? g_cur.latencySum / g_cur.latencyN : -1;
    g_last = s;
    g_cur = Window{};
    g_start = now;
}

}  // namespace

void NoteFlip(double workMs, double intervalMs) {
    std::lock_guard lk(g_mutex);
    Roll(Clock::now());
    ++g_cur.flips;
    g_cur.workSum += workMs;
    g_cur.intervalSum += intervalMs;
    g_cur.worst = std::max(g_cur.worst, intervalMs);
}

void NoteShown(bool generated, double latencyMs) {
    std::lock_guard lk(g_mutex);
    Roll(Clock::now());
    ++g_cur.shown;
    g_cur.generated += generated;
    if (!generated && latencyMs >= 0) {
        g_cur.latencySum += latencyMs;
        ++g_cur.latencyN;
    }
}

Snapshot Read() {
    std::lock_guard lk(g_mutex);
    Roll(Clock::now());
    return g_last;
}

}  // namespace video::live
