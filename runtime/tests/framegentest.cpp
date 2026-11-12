// Frame generation offline (video/framegen.h, LBP3_FRAMEGEN=1; perf/fg_present.md): the trivial
// generators, the drop rules, the display delay, the presenter's due-time queue, and the worker
// end to end with synthetic frames on the real clock. No window, no GPU, no game.
// Exit code 0 = all passed.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include "video/framegen.h"
#include "video/framegen_warp.h"
#include "video/presenter.h"

namespace fg = video::framegen;

static int g_checks = 0, g_failed = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL: %s\n", what);
    }
}

static int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static std::vector<uint8_t> Solid(uint32_t w, uint32_t h, uint8_t v) {
    return std::vector<uint8_t>(size_t(w) * h * 4, v);
}

static bool AllEqual(const std::vector<uint8_t>& px, uint8_t v) {
    for (uint8_t b : px)
        if (b != v) return false;
    return !px.empty();
}

static void TestBlend() {
    for (size_t bytes : {size_t(0), size_t(1), size_t(63), size_t(64), size_t(1000), size_t(4096 + 17)}) {
        std::vector<uint8_t> a(bytes), b(bytes), out(bytes, 0xCD);
        for (size_t i = 0; i < bytes; ++i) {
            a[i] = static_cast<uint8_t>(i * 7);
            b[i] = static_cast<uint8_t>(255 - i * 3);
        }
        fg::Blend(a.data(), b.data(), out.data(), bytes);
        bool ok = true;
        for (size_t i = 0; i < bytes; ++i) ok &= out[i] == static_cast<uint8_t>((unsigned(a[i]) + unsigned(b[i]) + 1) >> 1);
        Check(ok, "Blend is the rounded byte average at every size");
    }
}

static void TestGenerators() {
    Check(fg::MakeGenerator("blend") != nullptr, "blend generator exists");
    Check(fg::MakeGenerator("repeat") != nullptr, "repeat generator exists");
    Check(fg::MakeGenerator("nope") == nullptr, "unknown generator name gives nullptr");
    Check(std::strcmp(fg::MakeGenerator(nullptr)->Name(), "blend") == 0, "default generator is blend");
    const uint32_t w = 37, h = 11;
    auto a = Solid(w, h, 10), b = Solid(w, h, 61);
    fg::GenInputs in;
    in.prev = {a.data(), w, h};
    in.cur = {b.data(), w, h};
    std::vector<uint8_t> out;
    Check(fg::MakeGenerator("blend")->Generate(in, out) && AllEqual(out, 36), "blend gives (10 + 61 + 1) / 2 = 36");
    Check(out.size() == size_t(w) * h * 4, "blend output is width * height * 4");
    out.clear();
    Check(fg::MakeGenerator("repeat")->Generate(in, out) && AllEqual(out, 10), "repeat gives the previous frame");
}

static void TestDiff() {
    const uint32_t w = 64, h = 48;
    auto a = Solid(w, h, 10), b = Solid(w, h, 60);
    Check(fg::MeanAbsDiff({a.data(), w, h}, {a.data(), w, h}) == 0.0, "diff of a frame with itself is 0");
    Check(fg::MeanAbsDiff({a.data(), w, h}, {b.data(), w, h}) == 50.0, "diff of solid 10 and 60 is 50");
    Check(fg::MeanAbsDiff({a.data(), w, h}, {b.data(), w, h - 1}) == 0.0, "diff of different sizes is 0 (no cut call)");
    // Only B, G, R count: alpha differences are ignored.
    auto c = a;
    for (size_t i = 3; i < c.size(); i += 4) c[i] = 255;
    Check(fg::MeanAbsDiff({a.data(), w, h}, {c.data(), w, h}) == 0.0, "alpha doesn't count");
}

static void TestDecide() {
    fg::DecideIn d;
    d.havePrev = true;
    d.sameSize = true;
    d.periodNs = 33'333'333;
    d.sinceLastSlotNs = 33'333'333;
    d.simSteps = 1;
    d.cutDiff = 5;
    d.cutThreshold = 40;
    Check(fg::Decide(d) == fg::Drop::None, "a normal frame makes an in-between frame");
    auto e = d;
    e.havePrev = false;
    Check(fg::Decide(e) == fg::Drop::First, "no previous frame: first");
    e = d;
    e.sameSize = false;
    Check(fg::Decide(e) == fg::Drop::Size, "size changed: size");
    e = d;
    e.periodNs = 16'666'667;
    e.sinceLastSlotNs = 16'666'667;
    Check(fg::Decide(e) == fg::Drop::Rate, "the game at 60 Hz: rate");
    e = d;
    e.sinceLastSlotNs = 66'666'667;
    Check(fg::Decide(e) == fg::Drop::Late, "a slot late: late");
    e = d;
    e.sinceLastSlotNs = 49'000'000;
    Check(fg::Decide(e) == fg::Drop::None, "up to half a period late still counts as on the beat");
    e = d;
    e.sinceLastSlotNs = 10'000'000;
    Check(fg::Decide(e) == fg::Drop::Late, "far too soon: late");
    e = d;
    e.simSteps = 0;
    Check(fg::Decide(e) == fg::Drop::Steps, "0 sim steps: steps");
    e.simSteps = 2;
    Check(fg::Decide(e) == fg::Drop::Steps, "2 sim steps: steps");
    e.simSteps = -1;
    Check(fg::Decide(e) == fg::Drop::None, "unknown sim steps don't drop");
    e = d;
    e.cutDiff = 41;
    Check(fg::Decide(e) == fg::Drop::Cut, "a big difference: cut");
    Check(std::strcmp(fg::DropName(fg::Drop::GenLate), "gen-late") == 0, "drop names");
}

static void TestDelay() {
    Check(fg::DelayNs(165, 5) == 5'000'000, "LBP3_FRAMEGEN_DELAY_MS overrides");
    Check(fg::DelayNs(165, 0) == 0, "a zero override is allowed");
    Check(fg::DelayNs(165, -1) == int64_t(1e9 / 165) + 4'000'000, "default: one refresh + 4 ms");
    Check(fg::DelayNs(0, -1) == int64_t(1e9 / 60) + 4'000'000, "unknown refresh: 60 Hz");
}

static void Queue(int64_t dueNs, char kind, uint8_t v) {
    video::TimedFrame f;
    f.pixels = Solid(4, 4, v);
    f.width = f.height = 4;
    f.dueNs = dueNs;
    f.kind = kind;
    video::PresentTimed(f);
    Check(f.pixels.empty(), "PresentTimed takes the pixels");
}

static void TestQueue() {
    const int64_t ms = 1'000'000, refresh = 6 * ms;
    Queue(116 * ms, 'N', 2);
    Queue(100 * ms, 'G', 1);  // out of order: sorted by due time
    Queue(133 * ms, 'G', 3);
    video::TimedFrame t;
    int skipped = -1;
    Check(!video::PopDue(90 * ms, refresh, t, &skipped), "nothing due by 95 ms");
    Check(video::PopDue(95 * ms, refresh, t, &skipped) && t.dueNs == 100 * ms && t.kind == 'G' && skipped == 0,
          "the 100 ms frame is due by the refresh after 95 ms (100 <= 95 + 6 - 1)");
    Check(AllEqual(t.pixels, 1), "its pixels arrive");
    t = {};
    Check(video::PopDue(200 * ms, refresh, t, &skipped) && t.dueNs == 133 * ms && skipped == 1,
          "two due at once: the latest is shown, the other skipped");
    t = {};
    Check(!video::PopDue(1000 * ms, refresh, t, &skipped), "queue empty");
    // The window not taking frames: the queue keeps 8 and drops the oldest.
    for (int i = 0; i < 10; ++i) Queue((300 + i) * ms, 'N', static_cast<uint8_t>(i));
    Check(video::PopDue(1000 * ms, refresh, t, &skipped) && t.dueNs == 309 * ms && skipped == 7,
          "a full queue keeps the newest 8");
    uint64_t real = 0, gen = 0, skip = 0;
    video::TakePresentCounts(real, gen, skip);
    Check(skip == 2, "the two dropped from the full queue count as skipped");
    // Buffers come back through the spare pool.
    video::ReturnBuffer(std::vector<uint8_t>(16));
    Check(video::TakeBuffer().capacity() >= 16, "a returned buffer is handed out again");
    video::ReturnBuffer(std::vector<uint8_t>());  // empty ones are ignored
}

// Pops everything due by `dueNs` (as the presenter would at the refresh before it).
static bool PopBy(int64_t dueNs, video::TimedFrame& t) {
    const int64_t refresh = 16'666'667;
    int skipped = 0;
    return video::PopDue(dueNs - refresh + 1'000'000, refresh, t, &skipped);
}

static void WaitWorker(uint64_t wantSubmitted, fg::Stats& acc) {
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const fg::Stats s = fg::TakeStats();
        acc.submitted += s.submitted;
        acc.generated += s.generated;
        for (int k = 0; k < int(fg::Drop::kCount); ++k) acc.dropped[k] += s.dropped[k];
        uint64_t handled = acc.generated;
        for (uint64_t n : acc.dropped) handled += n;
        if (acc.submitted >= wantSubmitted && handled >= wantSubmitted) return;
    }
}

static void TestWorker() {
    _putenv_s("LBP3_FRAMEGEN", "1");
    _putenv_s("LBP3_FRAMEGEN_DELAY_MS", "8");
    _putenv_s("LBP3_FRAMEGEN_MODE", "blend");
    Check(fg::Enabled(), "LBP3_FRAMEGEN=1 turns it on");
    const uint32_t w = 32, h = 16;
    const int64_t period = 33'333'333, delay = 8'000'000;
    fg::Stats acc;
    // Frame A: the first, so no in-between frame.
    const int64_t t0 = NowNs();
    auto px = Solid(w, h, 90);
    const uint8_t* handed = px.data();
    fg::SubmitReal(px, w, h, {t0, period, 1});
    Check(px.data() != handed || px.empty(), "SubmitReal takes the frame and hands back a different (spare) buffer");
    WaitWorker(1, acc);
    // Frame B, one period later, on the real clock: an in-between frame (blend of 90 and 110; a
    // difference of 20 is under the cut threshold of 40).
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(t0 + period)));
    const int64_t tB = t0 + period;
    px = Solid(w, h, 110);
    fg::SubmitReal(px, w, h, {tB, period, 1});
    WaitWorker(2, acc);
    Check(acc.dropped[int(fg::Drop::First)] == 1 && acc.generated == 1, "A: first (dropped); B: generated");

    video::TimedFrame t;
    Check(PopBy(t0 + delay + period / 2, t) && t.kind == 'N' && AllEqual(t.pixels, 90) &&
              t.dueNs == t0 + delay + period / 2,
          "A is shown a delay plus half a period after its slot");
    t = {};
    Check(PopBy(tB + delay, t) && t.kind == 'G' && AllEqual(t.pixels, 100) && t.dueNs == tB + delay,
          "the in-between frame (90 + 110 + 1) / 2 = 100 is shown a delay after B's slot");
    t = {};
    Check(PopBy(tB + delay + period / 2, t) && t.kind == 'N' && AllEqual(t.pixels, 110),
          "B follows half a period later");
    t = {};

    // Frame C: two sim steps, so no in-between frame. Frame D: a cut (110 -> 0).
    const int64_t tC = tB + period;
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(tC)));
    px = Solid(w, h, 110);
    fg::SubmitReal(px, w, h, {tC, period, 2});
    WaitWorker(3, acc);
    const int64_t tD = tC + period;
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(tD)));
    px = Solid(w, h, 0);
    fg::SubmitReal(px, w, h, {tD, period, 1});
    WaitWorker(4, acc);
    Check(acc.dropped[int(fg::Drop::Steps)] == 1, "C (2 steps): dropped");
    Check(acc.dropped[int(fg::Drop::Cut)] == 1, "D (a cut): dropped");
    Check(acc.generated == 1, "still one generated");
    Check(PopBy(tD + delay + period / 2, t) && t.kind == 'N' && AllEqual(t.pixels, 0),
          "only real frames for C and D (the last one shown is D)");
    t = {};

    // Frame E arrives a slot late (a load): no in-between frame.
    const int64_t tE = tD + 2 * period;
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(tE)));
    px = Solid(w, h, 0);
    fg::SubmitReal(px, w, h, {tE, period, 1});
    WaitWorker(5, acc);
    Check(acc.dropped[int(fg::Drop::Late)] == 1, "E (late): dropped");

    // Frame F handed over so late that its in-between frame would land on the real frame's refresh.
    const int64_t tF = tE + period;
    std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(tF + 20'000'000)));
    px = Solid(w, h, 0);
    fg::SubmitReal(px, w, h, {tF, period, 1});
    WaitWorker(6, acc);
    Check(acc.dropped[int(fg::Drop::GenLate)] == 1, "F (made too late): dropped");
    PopBy(tF + delay + period / 2 + 100'000'000, t);  // drain
}

// A textured frame (hash noise, 3x3 box-blurred) shifted by (dx, dy): the warp generator's
// midpoint must match the frame shifted by half that in the interior, identical frames must come
// out unchanged, and the game's vectors (when given) must not make a pure pan worse.
static uint8_t Noise(int x, int y) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return uint8_t((h ^ (h >> 16)) & 255);
}
static std::vector<uint8_t> Textured(uint32_t w, uint32_t h, int dx, int dy) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            int sum = 0;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) sum += Noise(int(x) - dx + i, int(y) - dy + j);
            const uint8_t v = uint8_t(sum / 9);
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            p[0] = v;
            p[1] = uint8_t(255 - v);
            p[2] = uint8_t(v / 2 + 64);
            p[3] = 255;
        }
    return px;
}
// The map's in-world text over its white stripes (perf/fg_gpu.md, "Loading..."): horizontal
// white/blue stripes moved down by bgDy, and a white bar glyph with a darker outline at
// (gx, gy). Where a white stripe pixel is the same in both frames next to the glyph, the
// generator must not copy it as static (that erased the letters) but draw it moved with the
// scene (framegen_warp.h, textKeep).
static std::vector<uint8_t> TextScene(uint32_t w, uint32_t h, int bgDy, int gx, int gy) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const bool white = ((int(y) - bgDy + 480) / 24) % 2 == 0;
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            p[0] = white ? 255 : 90, p[1] = white ? 255 : 90, p[2] = white ? 255 : 230, p[3] = 255;
        }
    for (int y = gy; y < gy + 72; ++y)
        for (int x = gx - 3; x < gx + 9; ++x) {
            if (x < 0 || y < 0 || x >= int(w) || y >= int(h)) continue;
            uint8_t* p = &px[(size_t(y) * w + x) * 4];
            const bool core = x >= gx && x < gx + 6;
            p[0] = core ? 255 : 170, p[1] = core ? 255 : 170, p[2] = core ? 255 : 230;
        }
    return px;
}
static double InteriorMae(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint32_t w, uint32_t h, int m) {
    double sum = 0;
    size_t n = 0;
    for (uint32_t y = m; y < h - m; ++y)
        for (uint32_t x = m; x < w - m; ++x)
            for (int c = 0; c < 3; ++c) {
                const size_t i = (size_t(y) * w + x) * 4 + c;
                sum += std::abs(int(a[i]) - int(b[i]));
                ++n;
            }
    return n ? sum / double(n) : 0;
}
static void TestWarp() {
    const uint32_t w = 320, h = 192;
    auto g = fg::MakeGenerator("warp");
    Check(g != nullptr && std::strcmp(g->Name(), "warp") == 0, "warp generator exists");
    if (!g) return;
    for (const auto& d : {std::pair<int, int>{8, 0}, {0, 8}, {-12, 4}, {20, -6}}) {
        const auto a = Textured(w, h, 0, 0), b = Textured(w, h, d.first, d.second);
        const auto truth = Textured(w, h, d.first / 2, d.second / 2);
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {b.data(), w, h};
        std::vector<uint8_t> out;
        Check(g->Generate(in, out) && out.size() == a.size(), "warp generates a frame");
        const double warpErr = InteriorMae(out, truth, w, h, 32);
        std::vector<uint8_t> blend(a.size());
        fg::Blend(a.data(), b.data(), blend.data(), a.size());
        const double blendErr = InteriorMae(blend, truth, w, h, 32);
        char what[160];
        std::snprintf(what, sizeof what, "warp midpoint of a (%d,%d) pan within 2 levels (got %.2f; blend %.2f)", d.first,
                      d.second, warpErr, blendErr);
        Check(warpErr < 2.0 && warpErr < blendErr / 4, what);
        // The game's vectors for the same pan (B/G codes = 127 + motion / scale; R = covered).
        std::vector<uint8_t> vec(a.size());
        for (size_t i = 0; i < vec.size(); i += 4) {
            vec[i] = 255;
            vec[i + 1] = uint8_t(127 + int(std::lround(d.second / 0.529)));
            vec[i + 2] = uint8_t(127 + int(std::lround(d.first / 0.941)));
            vec[i + 3] = 255;
        }
        in.motionVectors = vec.data();
        std::vector<uint8_t> out2;
        Check(g->Generate(in, out2), "warp with the game's vectors generates a frame");
        std::snprintf(what, sizeof what, "the game's vectors keep a (%d,%d) pan right (got %.2f)", d.first, d.second,
                      InteriorMae(out2, truth, w, h, 32));
        Check(InteriorMae(out2, truth, w, h, 32) < 2.0, what);
    }
    {
        const auto a = Textured(w, h, 3, 5);
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {a.data(), w, h};
        std::vector<uint8_t> out;
        Check(g->Generate(in, out) && out == a, "warp of two identical frames is that frame");
    }
    {
        // Saturated vectors everywhere (a cut): the generator declines.
        const auto a = Textured(w, h, 0, 0), b = Textured(w, h, 40, 0);
        std::vector<uint8_t> vec(a.size(), 0);
        for (size_t i = 0; i < vec.size(); i += 4) vec[i] = 255, vec[i + 1] = 255, vec[i + 2] = 0;
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {b.data(), w, h};
        in.motionVectors = vec.data();
        std::vector<uint8_t> out;
        Check(!g->Generate(in, out), "warp declines when every covered vector is saturated");
    }
    {
        // A HUD drawn after the composite comes back from N exactly; elsewhere N's small display
        // difference is carried over.
        const auto a = Textured(w, h, 0, 0), b = Textured(w, h, 8, 0);
        auto fin = b;
        for (uint32_t y = 10; y < 30; ++y)
            for (uint32_t x = 10; x < 120; ++x) {
                uint8_t* p = &fin[(size_t(y) * w + x) * 4];
                p[0] = 250, p[1] = 250, p[2] = 250;
            }
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {fin.data(), w, h};
        in.hudlessPrev = a.data();
        in.hudless = b.data();
        std::vector<uint8_t> out;
        bool hudOk = g->Generate(in, out);
        for (uint32_t y = 10; y < 30 && hudOk; ++y)
            for (uint32_t x = 10; x < 120; ++x)
                hudOk &= out[(size_t(y) * w + x) * 4] == 250 && out[(size_t(y) * w + x) * 4 + 1] == 250;
        Check(hudOk, "the screen HUD comes back from the real frame");
        const auto truth = Textured(w, h, 4, 0);
        double err = 0;
        size_t n = 0;
        for (uint32_t y = 60; y < h - 32; ++y)
            for (uint32_t x = 32; x < w - 32; ++x)
                for (int c = 0; c < 3; ++c, ++n)
                    err += std::abs(int(out[(size_t(y) * w + x) * 4 + c]) - int(truth[(size_t(y) * w + x) * 4 + c]));
        Check(n && err / double(n) < 2.0, "below the HUD the HUD-less images are interpolated");
    }
    {
        // In-world text over white stripes: drawn from the nearer frame, not copied as static.
        const uint32_t tw = 320, th = 192;
        const auto a = TextScene(tw, th, 0, 150, 60), b = TextScene(tw, th, 8, 156, 68);
        fg::GenInputs in;
        in.prev = {a.data(), tw, th};
        in.cur = {b.data(), tw, th};
        std::vector<uint8_t> on, off;
        const bool okOn = g->Generate(in, on);
        const fg::WarpDebug* dOn = fg::LastWarpDebug(g.get());
        const uint64_t textOn = dOn ? dOn->pixText : 0;
        _putenv_s("LBP3_FRAMEGEN_TEXT", "off");
        auto gOff = fg::MakeGenerator("warp");
        _putenv_s("LBP3_FRAMEGEN_TEXT", "");
        const bool okOff = gOff && gOff->Generate(in, off);
        const fg::WarpDebug* dOff = gOff ? fg::LastWarpDebug(gOff.get()) : nullptr;
        char what[160];
        std::snprintf(what, sizeof what, "white pixels near in-world text are drawn moved, not copied (%llu px)",
                      (unsigned long long)textOn);
        Check(okOn && textOn > 0, what);
        Check(okOff && dOff && dOff->pixText == 0 && off != on, "LBP3_FRAMEGEN_TEXT=off keeps the old static copy");
    }
}

// The GPU warp (perf/fg_gpu.md) gives the CPU warp's bytes and decisions in every case above.
static void TestWarpGpu() {
    const uint32_t w = 320, h = 192;
    auto cpu = fg::MakeGenerator("warp-cpu");
    auto gpu = fg::MakeGenerator("warp-gpu");
    Check(gpu != nullptr && std::strcmp(gpu->Name(), "warp-gpu") == 0, "the GPU warp generator runs on this machine");
    if (!cpu || !gpu) return;
    auto same = [&](const fg::GenInputs& in, const char* what) {
        std::vector<uint8_t> a, b;
        const bool okA = cpu->Generate(in, a), okB = gpu->Generate(in, b);
        Check(okA == okB && (!okA || a == b), what);
    };
    for (const auto& d : {std::pair<int, int>{8, 0}, {0, 8}, {-12, 4}, {20, -6}}) {
        const auto a = Textured(w, h, 0, 0), b = Textured(w, h, d.first, d.second);
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {b.data(), w, h};
        char what[128];
        std::snprintf(what, sizeof what, "GPU warp = CPU warp for a (%d,%d) pan", d.first, d.second);
        same(in, what);
        std::vector<uint8_t> vec(a.size());
        for (size_t i = 0; i < vec.size(); i += 4) {
            vec[i] = 255;
            vec[i + 1] = uint8_t(127 + int(std::lround(d.second / 0.529)));
            vec[i + 2] = uint8_t(127 + int(std::lround(d.first / 0.941)));
            vec[i + 3] = 255;
        }
        in.motionVectors = vec.data();
        std::snprintf(what, sizeof what, "GPU warp = CPU warp for a (%d,%d) pan with the game's vectors", d.first, d.second);
        same(in, what);
    }
    {
        const auto a = Textured(w, h, 0, 0), b = Textured(w, h, 40, 0);
        std::vector<uint8_t> vec(a.size(), 0);
        for (size_t i = 0; i < vec.size(); i += 4) vec[i] = 255, vec[i + 1] = 255, vec[i + 2] = 0;
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {b.data(), w, h};
        in.motionVectors = vec.data();
        std::vector<uint8_t> out;
        Check(!gpu->Generate(in, out), "the GPU warp declines when every covered vector is saturated");
    }
    {
        const auto a = Textured(w, h, 0, 0), b = Textured(w, h, 8, 0);
        auto fin = b;
        for (uint32_t y = 10; y < 30; ++y)
            for (uint32_t x = 10; x < 120; ++x) {
                uint8_t* p = &fin[(size_t(y) * w + x) * 4];
                p[0] = 250, p[1] = 250, p[2] = 250;
            }
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {fin.data(), w, h};
        in.hudlessPrev = a.data();
        in.hudless = b.data();
        same(in, "GPU warp = CPU warp with the HUD-less images and a screen HUD");
    }
    {
        const auto a = TextScene(w, h, 0, 150, 60), b = TextScene(w, h, 8, 156, 68);
        fg::GenInputs in;
        in.prev = {a.data(), w, h};
        in.cur = {b.data(), w, h};
        same(in, "GPU warp = CPU warp for in-world text over white stripes");
    }
    // A camera cut by the camera's jump (perf/fg_inputs.md: 5,753 at a cut, at most ~250 in play).
    fg::DecideIn d;
    d.havePrev = d.sameSize = true;
    d.periodNs = d.sinceLastSlotNs = 33'333'333;
    d.simSteps = 1;
    d.camJumpThreshold = 1000;
    d.camJump = 250;
    Check(fg::Decide(d) == fg::Drop::None, "a 250-unit camera move is not a cut");
    d.camJump = 5753;
    Check(fg::Decide(d) == fg::Drop::Cut, "a 5,753-unit camera jump is a cut");
    d.prevCamJump = 240;
    Check(fg::Decide(d) == fg::Drop::Cut, "a 5,753-unit jump after a 240-unit move is a cut");
    d.prevCamJump = 4804;
    d.camJump = 5453;
    Check(fg::Decide(d) == fg::Drop::None, "a fast but smooth camera move (the map swoop) is not a cut");
    d.camJumpThreshold = 0;
    Check(fg::Decide(d) == fg::Drop::None, "the camera-jump rule can be turned off");
}

int main() {
    TestBlend();
    TestGenerators();
    TestDiff();
    TestDecide();
    TestDelay();
    TestQueue();
    TestWorker();
    TestWarp();
    TestWarpGpu();
    std::printf("framegentest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
