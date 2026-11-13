// Tests (tests/clocktest.cpp): every game-visible clock of libkernel, read at once and converted to
// seconds with the frequency the game is told, so a test can check they all advance at 1 s per
// second. CPU-time clocks are left out (they advance with work, not time). Ours.
#pragma once
#include <string>
#include <vector>

namespace hle::kernel {

struct ClockSample {
    std::string name;
    double seconds;
};
std::vector<ClockSample> SampleClocks();

}  // namespace hle::kernel
