// LBP3_WAIT_LOG=1: how late the game's timed waits wake (requested against actual), one "[wait]" line
// every 5 s: the game's sleeps (sceKernelUsleep, nanosleep), sceNetEpollWait calls that time out, and
// the 1-ms polls inside them. On a busy PC a 1-ms wait can turn into 15-30 ms, which games' network
// code feels. Off by default (nothing is measured). Ours.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "core/log_stamp.h"

namespace core::waitstats {

enum Kind { kSleep, kEpollWait, kEpollPoll, kKinds };

struct Bucket {
    std::atomic<uint64_t> n{0}, lateSumUs{0}, over5{0}, over15{0};
    std::atomic<int64_t> lateMaxUs{0};
};
inline Bucket g_buckets[kKinds];
inline std::atomic<int64_t> g_lastReportUs{0};

inline bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_WAIT_LOG");
        return v && *v == '1';
    }();
    return on;
}

inline int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline void Report() {
    static const char* const names[kKinds] = {"sleeps", "epoll waits timed out", "epoll 1-ms polls"};
    char line[512];
    int at = std::snprintf(line, sizeof(line), "[wait] %s last 5 s, how late:", Stamp().text);
    for (int k = 0; k < kKinds; ++k) {
        Bucket& b = g_buckets[k];
        const uint64_t n = b.n.exchange(0), sum = b.lateSumUs.exchange(0), o5 = b.over5.exchange(0),
                       o15 = b.over15.exchange(0);
        const int64_t mx = b.lateMaxUs.exchange(0);
        if (!n) continue;
        at += std::snprintf(line + at, sizeof(line) - at, " %s %llu, avg %.2f max %.1f ms (%llu over 5 ms, %llu over 15);",
                            names[k], static_cast<unsigned long long>(n), sum / 1000.0 / n, mx / 1000.0,
                            static_cast<unsigned long long>(o5), static_cast<unsigned long long>(o15));
        if (at >= static_cast<int>(sizeof(line))) break;
    }
    std::fprintf(stderr, "%s\n", line);
}

// A timed wait that asked for `requestedUs` and took `actualUs`.
inline void Note(Kind k, int64_t requestedUs, int64_t actualUs) {
    const int64_t late = std::max<int64_t>(0, actualUs - requestedUs);
    Bucket& b = g_buckets[k];
    ++b.n;
    b.lateSumUs += static_cast<uint64_t>(late);
    if (late > 5000) ++b.over5;
    if (late > 15000) ++b.over15;
    int64_t m = b.lateMaxUs.load(std::memory_order_relaxed);
    while (late > m && !b.lateMaxUs.compare_exchange_weak(m, late)) {
    }
    const int64_t now = NowUs();
    int64_t last = g_lastReportUs.load(std::memory_order_relaxed);
    if (last == 0) {
        g_lastReportUs.compare_exchange_strong(last, now);
    } else if (now - last >= 5'000'000 && g_lastReportUs.compare_exchange_strong(last, now)) {
        Report();
    }
}

}  // namespace core::waitstats
