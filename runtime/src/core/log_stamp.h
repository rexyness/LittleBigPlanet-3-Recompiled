// Wall-clock stamps for log lines: "hh:mm:ss.mmm", local time, so the logs of two game instances
// on one PC (the co-op tests) line up. Ours.
#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>

namespace core {

struct Stamp {
    char text[16];
    Stamp() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        const int ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::snprintf(text, sizeof(text), "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    }
};

}  // namespace core
