// The window's upscaler offline (gpu/upscale, LBP3_UPSCALE=fsr1; perf/upscale.md in the planning
// repo): the size rules, and the FSR 1 passes on a headless device against what they must do:
// keep flat colour flat, keep the channels where they are, put edges where bilinear puts them
// (no half-pixel shift in either direction), stay close to bilinear on smooth content, and
// sharpen more at lower RCAS values.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gpu/upscale_offline.h"
#include "gpu/upscale_size.h"

namespace up = gpu::upscale;

static int g_checks = 0, g_failed = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL: %s\n", what);
    }
}

static void TestTarget() {
    const up::Size f{1920, 1080};
    Check(up::TargetFor(f, {2560, 1440}) == up::Size{2560, 1440}, "1440p window: upscale to 2560x1440");
    Check(up::TargetFor(f, {3840, 2160}) == up::Size{3840, 2160}, "4K window: upscale to 3840x2160");
    Check(up::TargetFor(f, {1920, 1080}).Empty(), "a 1080p window: not upscaled");
    Check(up::TargetFor(f, {1280, 720}).Empty(), "a smaller window: not upscaled (the window shrinks it)");
    Check(up::TargetFor(f, {2560, 1000}).Empty(), "wider but shorter than the frame: not upscaled");
    Check(up::TargetFor(f, {1920, 1200}) == up::Size{1920, 1200}, "taller only: upscaled to the window");
    Check(up::TargetFor(f, {0, 0}).Empty(), "a minimised window: not upscaled");
    Check(up::TargetFor(f, {5120, 2880}).Empty(), "beyond 4096 a side: not upscaled");
    Check(up::TargetFor({}, {2560, 1440}).Empty(), "no frame yet: not upscaled");
}

static void TestDebounce() {
    up::Debounce d;
    const up::Size a{2560, 1440}, b{2000, 1200}, c{2100, 1250};
    Check(d.Update(a) == a, "the first target takes effect at once");
    bool held = true;
    for (int i = 0; i < d.frames - 1; ++i) held = held && d.Update(b) == a;
    Check(held, "a new target is held back for frames - 1 frames");
    Check(d.Update(b) == b, "and takes effect on the frames-th frame");
    // Dragging: the size changes every frame, so nothing switches until it settles.
    bool steady = true;
    for (int i = 0; i < 40; ++i) steady = steady && d.Update(i % 2 ? a : c) == b;
    Check(steady, "a size that keeps changing never takes effect");
    for (int i = 0; i < d.frames - 1; ++i) d.Update(c);
    Check(d.Update(b) == b, "going back to the current size cancels the pending one");
    Check(d.Update(up::Size{}) == b, "going to 'none' is debounced too");
}

static uint32_t Px(const std::vector<uint8_t>& img, uint32_t w, uint32_t x, uint32_t y, int c) {
    return img[(size_t(y) * w + x) * 4 + c];
}

// Bilinear 4-byte upscale with clamp-to-edge, sampling at pixel centres (what the window's
// stretch does), for one channel.
static double Bilinear(const std::vector<uint8_t>& in, uint32_t iw, uint32_t ih, uint32_t ow, uint32_t oh, uint32_t x,
                       uint32_t y, int c) {
    const double u = (x + 0.5) * double(iw) / ow - 0.5, v = (y + 0.5) * double(ih) / oh - 0.5;
    const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    const double fx = u - x0, fy = v - y0;
    auto at = [&](int xx, int yy) {
        xx = std::clamp(xx, 0, int(iw) - 1);
        yy = std::clamp(yy, 0, int(ih) - 1);
        return double(in[(size_t(yy) * iw + xx) * 4 + c]);
    };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

// Where along a row (or column) the value first crosses the midpoint between lo and hi.
static double Crossing(const std::vector<double>& v, double lo, double hi) {
    const double mid = (lo + hi) / 2;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i - 1] - mid) * (v[i] - mid) <= 0 && v[i - 1] != v[i]) return double(i - 1) + (mid - v[i - 1]) / (v[i] - v[i - 1]);
    return -1;
}

static void TestGpu() {
    gpu::upgpu::OfflineUpscaler u;
    if (!u.Ok()) {
        std::printf("note: no Vulkan device for the GPU checks (skipped)\n");
        return;
    }
    const uint32_t iw = 1920, ih = 1080, ow = 2560, oh = 1440;
    std::vector<uint8_t> in(size_t(iw) * ih * 4), out;
    double ms = -1;

    // Flat colour stays flat, channels stay in place, alpha becomes 255.
    for (size_t i = 0; i < in.size(); i += 4) in[i] = 0x10, in[i + 1] = 0x40, in[i + 2] = 0xC0, in[i + 3] = 0x00;
    bool ran = u.Run(in.data(), iw, ih, ow, oh, 0.25f, out, ms);
    Check(ran && out.size() == size_t(ow) * oh * 4, "upscales 1920x1080 to 2560x1440 (output size)");
    if (!ran) return;
    bool flat = true;
    for (size_t i = 0; i < out.size(); i += 4)
        flat = flat && std::abs(int(out[i]) - 0x10) <= 1 && std::abs(int(out[i + 1]) - 0x40) <= 1 &&
               std::abs(int(out[i + 2]) - 0xC0) <= 1 && out[i + 3] == 255;
    Check(flat, "flat colour stays flat (within 1), channels in place, alpha 255");

    // A vertical step edge between input columns 959 and 960: the transition lands where
    // bilinear puts it (x about 1279.7), in every channel's own direction.
    for (uint32_t y = 0; y < ih; ++y)
        for (uint32_t x = 0; x < iw; ++x) {
            uint8_t* p = &in[(size_t(y) * iw + x) * 4];
            const bool right = x >= 960;
            p[0] = right ? 230 : 20, p[1] = right ? 20 : 230, p[2] = right ? 200 : 40, p[3] = 255;
        }
    u.Run(in.data(), iw, ih, ow, oh, 0.25f, out, ms);
    for (int c = 0; c < 3; ++c) {
        std::vector<double> row(ow), ref(ow);
        for (uint32_t x = 0; x < ow; ++x) row[x] = Px(out, ow, x, 700, c), ref[x] = Bilinear(in, iw, ih, ow, oh, x, 700, c);
        const double lo = Px(in, iw, 0, 525, c), hi = Px(in, iw, iw - 1, 525, c);
        const double xf = Crossing(row, lo, hi), xb = Crossing(ref, lo, hi);
        char what[160];
        std::snprintf(what, sizeof what, "vertical edge, channel %d: FSR crosses at x=%.2f, bilinear at %.2f (within 0.75)", c,
                      xf, xb);
        Check(xf >= 0 && std::fabs(xf - xb) <= 0.75, what);
    }

    // A horizontal step edge between input rows 539 and 540 (checks the vertical direction).
    for (uint32_t y = 0; y < ih; ++y)
        for (uint32_t x = 0; x < iw; ++x) {
            uint8_t* p = &in[(size_t(y) * iw + x) * 4];
            const uint8_t v = y >= 540 ? 220 : 30;
            p[0] = p[1] = p[2] = v, p[3] = 255;
        }
    u.Run(in.data(), iw, ih, ow, oh, 0.25f, out, ms);
    {
        std::vector<double> col(oh), ref(oh);
        for (uint32_t y = 0; y < oh; ++y) col[y] = Px(out, ow, 1000, y, 1), ref[y] = Bilinear(in, iw, ih, ow, oh, 1000, y, 1);
        const double yf = Crossing(col, 30, 220), yb = Crossing(ref, 30, 220);
        char what[160];
        std::snprintf(what, sizeof what, "horizontal edge: FSR crosses at y=%.2f, bilinear at %.2f (within 0.75)", yf, yb);
        Check(yf >= 0 && std::fabs(yf - yb) <= 0.75, what);
    }

    // Smooth content: close to bilinear everywhere (no misplaced taps); sharper RCAS = more
    // contrast at a soft edge.
    for (uint32_t y = 0; y < ih; ++y)
        for (uint32_t x = 0; x < iw; ++x) {
            uint8_t* p = &in[(size_t(y) * iw + x) * 4];
            p[0] = uint8_t(128 + 100 * std::sin(x * 0.01) * std::cos(y * 0.013));
            p[1] = uint8_t(std::min(255u, x * 255u / iw));
            p[2] = uint8_t(std::min(255u, y * 255u / ih));
            p[3] = 255;
        }
    u.Run(in.data(), iw, ih, ow, oh, 0.25f, out, ms);
    double sum = 0, worst = 0;
    size_t n = 0;
    for (uint32_t y = 2; y < oh - 2; y += 7)
        for (uint32_t x = 2; x < ow - 2; x += 5)
            for (int c = 0; c < 3; ++c) {
                const double d = std::fabs(Px(out, ow, x, y, c) - Bilinear(in, iw, ih, ow, oh, x, y, c));
                sum += d;
                worst = std::max(worst, d);
                ++n;
            }
    char what[160];
    std::snprintf(what, sizeof what, "smooth content: mean |FSR - bilinear| %.2f (<= 1.5), worst %.1f (<= 6)", sum / n, worst);
    Check(sum / n <= 1.5 && worst <= 6, what);

    // A 3-texel soft edge, at RCAS 0 (sharpest) and 2 stops: the sharper one has the steeper slope.
    for (uint32_t y = 0; y < ih; ++y)
        for (uint32_t x = 0; x < iw; ++x) {
            uint8_t* p = &in[(size_t(y) * iw + x) * 4];
            const int t = std::clamp(int(x) - 958, 0, 3);
            p[0] = p[1] = p[2] = uint8_t(40 + t * 50), p[3] = 255;
        }
    std::vector<uint8_t> sharp, soft;
    u.Run(in.data(), iw, ih, ow, oh, 0.0f, sharp, ms);
    u.Run(in.data(), iw, ih, ow, oh, 2.0f, soft, ms);
    int slopeSharp = 0, slopeSoft = 0;
    for (uint32_t x = 1270; x < 1290; ++x) {
        slopeSharp = std::max(slopeSharp, int(Px(sharp, ow, x + 1, 300, 1)) - int(Px(sharp, ow, x, 300, 1)));
        slopeSoft = std::max(slopeSoft, int(Px(soft, ow, x + 1, 300, 1)) - int(Px(soft, ow, x, 300, 1)));
    }
    std::snprintf(what, sizeof what, "RCAS 0 stops is sharper than 2 stops (steepest step %d against %d)", slopeSharp,
                  slopeSoft);
    Check(slopeSharp > slopeSoft, what);

    // GPU time (reported, not checked): the passes and the readback copy.
    for (const auto& s : {std::pair{2560u, 1440u}, std::pair{3840u, 2160u}}) {
        double total = 0, best = 1e9;
        int k = 0;
        for (int i = 0; i < 12; ++i) {
            if (!u.Run(in.data(), iw, ih, s.first, s.second, 0.25f, out, ms) || ms < 0) continue;
            if (i < 2) continue;  // warm-up
            total += ms, best = std::min(best, ms), ++k;
        }
        if (k) std::printf("gpu: 1920x1080 -> %ux%u: %.3f ms avg, %.3f best (passes + readback copy)\n", s.first, s.second,
                           total / k, best);
    }
}

int main() {
    TestTarget();
    TestDebounce();
    TestGpu();
    std::printf("upscaletest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
