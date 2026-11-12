// Frame generation offline (video/framegen.h, LBP3_FRAMEGEN=1; perf/fg_present.md): the trivial
// generators, the drop rules, the display delay, the presenter's due-time queue, and the worker
// end to end with synthetic frames on the real clock. No window, no GPU, no game.
// Exit code 0 = all passed.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "video/framegen.h"
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

int main() {
    TestBlend();
    TestGenerators();
    TestDiff();
    TestDecide();
    TestDelay();
    TestQueue();
    TestWorker();
    std::printf("framegentest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
