// clocktest: every game-visible clock (libkernel's clock_gettime ids, gettimeofday, process time,
// the TSC over its stated frequency, libSceRtc's current tick) advances 1.0 s per second of real
// time, within 1%. A clock off its stated rate would make a game's timers (its network
// retransmissions, for one) fire early, late or never. Ours.
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "hle/clock_check.h"

#define SYSV __attribute__((sysv_abi))
namespace hle::rtc {
struct Tick {
    uint64_t tick;
};
int SYSV sceRtcGetCurrentTick(Tick* tick);
int SYSV sceRtcGetCurrentNetworkTick(Tick* tick);
}  // namespace hle::rtc

static int g_checks = 0, g_failed = 0;

static void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failed;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

int main() {
    using Clock = std::chrono::steady_clock;
    // A second of real time, measured by the host's steady clock around each pair of readings.
    hle::rtc::Tick t0{}, n0{}, t1{}, n1{};
    const auto r0 = Clock::now();
    const auto a = hle::kernel::SampleClocks();
    hle::rtc::sceRtcGetCurrentTick(&t0);
    hle::rtc::sceRtcGetCurrentNetworkTick(&n0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const auto b = hle::kernel::SampleClocks();
    hle::rtc::sceRtcGetCurrentTick(&t1);
    hle::rtc::sceRtcGetCurrentNetworkTick(&n1);
    const auto r1 = Clock::now();
    const double real = std::chrono::duration<double>(r1 - r0).count();
    std::printf("real time: %.6f s\n", real);

    Check(a.size() == b.size() && !a.empty(), "the same clocks both times (" + std::to_string(a.size()) + ")");
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const double d = b[i].seconds - a[i].seconds;
        // SECOND counts whole seconds: it may advance 1 or 2 in a little over a second.
        const bool whole = a[i].name.find("SECOND") != std::string::npos;
        const bool ok = whole ? (d >= 1.0 && d <= 2.0) : std::fabs(d / real - 1.0) <= 0.01;
        char line[200];
        std::snprintf(line, sizeof(line), "%-45s advanced %.6f s (%.3f%% of real)", a[i].name.c_str(), d,
                      100.0 * d / real);
        Check(ok, line);
    }
    // libSceRtc ticks are microseconds (sceRtcGetTickResolution is 1,000,000 on the PS4).
    for (const auto& [name, d] : {std::pair{"sceRtcGetCurrentTick", (t1.tick - t0.tick) / 1e6},
                                  std::pair{"sceRtcGetCurrentNetworkTick", (n1.tick - n0.tick) / 1e6}}) {
        char line[200];
        std::snprintf(line, sizeof(line), "%-45s advanced %.6f s at 1,000,000 ticks/s (%.3f%% of real)", name, d,
                      100.0 * d / real);
        Check(std::fabs(d / real - 1.0) <= 0.01, line);
    }
    std::printf("clocktest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
