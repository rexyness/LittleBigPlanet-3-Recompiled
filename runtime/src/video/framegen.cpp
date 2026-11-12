#include "video/framegen.h"

#include <emmintrin.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "video/framegen_warp.h"
#include "video/presenter.h"

namespace video::framegen {
namespace {

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

double EnvDouble(const char* name, double fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    char* end = nullptr;
    const double d = std::strtod(v, &end);
    return end != v ? d : fallback;
}

class BlendGenerator final : public Generator {
public:
    const char* Name() const override { return "blend"; }
    bool Generate(const GenInputs& in, std::vector<uint8_t>& out) override {
        const size_t bytes = size_t(in.cur.width) * in.cur.height * 4;
        out.resize(bytes);
        Blend(in.prev.pixels, in.cur.pixels, out.data(), bytes);
        return true;
    }
};

class RepeatGenerator final : public Generator {
public:
    const char* Name() const override { return "repeat"; }
    bool Generate(const GenInputs& in, std::vector<uint8_t>& out) override {
        const size_t bytes = size_t(in.prev.width) * in.prev.height * 4;
        out.resize(bytes);
        std::memcpy(out.data(), in.prev.pixels, bytes);
        return true;
    }
};

struct Submission {
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    RealInfo info;
};

std::mutex g_mutex;  // the submission queue and the stats
std::condition_variable g_cv;
std::deque<Submission> g_queue;
Stats g_stats;
std::string g_generatorName;

void CountDrop(Drop d) {
    std::lock_guard lk(g_mutex);
    ++g_stats.dropped[static_cast<int>(d)];
}

void Worker() {
    const char* mode = std::getenv("LBP3_FRAMEGEN_MODE");
    std::unique_ptr<Generator> gen = MakeGenerator(mode && *mode ? mode : "blend");
    if (!gen) {
        std::fprintf(stderr, "[framegen] unknown LBP3_FRAMEGEN_MODE=%s; using blend\n", mode);
        gen = MakeGenerator("blend");
    }
    const double delayOverride = EnvDouble("LBP3_FRAMEGEN_DELAY_MS", -1.0);
    const double cutThreshold = EnvDouble("LBP3_FRAMEGEN_CUT", 40.0);
    {
        std::lock_guard lk(g_mutex);
        g_generatorName = gen->Name();
    }
    std::fprintf(stderr, "[framegen] on: generator %s, delay %s, cut threshold %.1f\n", gen->Name(),
                 delayOverride >= 0 ? std::to_string(delayOverride).c_str() : "one refresh + 4 ms", cutThreshold);

    std::vector<uint8_t> prev;  // the last real frame (a copy: its own buffer goes to the presenter)
    uint32_t prevW = 0, prevH = 0;
    int64_t prevSlot = 0;
    bool havePrev = false;
    for (;;) {
        Submission s;
        {
            std::unique_lock lk(g_mutex);
            g_cv.wait(lk, [] { return !g_queue.empty(); });
            s = std::move(g_queue.front());
            g_queue.pop_front();
        }
        const double hz = RefreshHz();
        const int64_t refreshNs = hz > 0 ? static_cast<int64_t>(1e9 / hz) : 16'666'667;
        const int64_t delayNs = DelayNs(hz, delayOverride);
        const int64_t genDue = s.info.slotNs + delayNs;
        const int64_t realDue = genDue + s.info.periodNs / 2;

        const HostFrame cur{s.pixels.data(), s.width, s.height};
        const HostFrame old{prev.data(), prevW, prevH};
        DecideIn d;
        d.havePrev = havePrev;
        d.sameSize = havePrev && prevW == s.width && prevH == s.height && prev.size() == s.pixels.size();
        d.sinceLastSlotNs = s.info.slotNs - prevSlot;
        d.periodNs = s.info.periodNs;
        d.simSteps = s.info.simSteps;
        d.cutThreshold = cutThreshold;
        if (d.sameSize) {
            d.cutDiff = MeanAbsDiff(old, cur);
            std::lock_guard lk(g_mutex);
            g_stats.diffSum += d.cutDiff;
            g_stats.diffMax = std::max(g_stats.diffMax, d.cutDiff);
            ++g_stats.diffCount;
        }
        Drop drop = Decide(d);
        if (drop == Drop::None) {
            TimedFrame g;
            g.pixels = TakeBuffer();
            const int64_t t0 = NowNs();
            GenInputs in;
            in.prev = old;
            in.cur = cur;
            const bool ok = gen->Generate(in, g.pixels);
            const int64_t t1 = NowNs();
            {
                std::lock_guard lk(g_mutex);
                const double ms = double(t1 - t0) / 1e6;
                g_stats.genMsSum += ms;
                g_stats.genMsMax = std::max(g_stats.genMsMax, ms);
            }
            if (!ok) {
                drop = Drop::Generator;
            } else if (t1 > realDue - refreshNs) {
                drop = Drop::GenLate;  // it would land on or after the real frame's refresh
            } else {
                g.width = s.width;
                g.height = s.height;
                g.dueNs = genDue;
                g.slotNs = s.info.slotNs;
                g.queuedNs = t1;
                g.kind = 'G';
                PresentTimed(g);
                std::lock_guard lk(g_mutex);
                ++g_stats.generated;
                g_stats.genLateShown += t1 > genDue - refreshNs;  // shown, but a refresh late
            }
            ReturnBuffer(std::move(g.pixels));
        }
        if (drop != Drop::None) CountDrop(drop);

        // Keep a copy as the next frame's N-1, then queue the real frame itself.
        prev.resize(s.pixels.size());
        if (!s.pixels.empty()) std::memcpy(prev.data(), s.pixels.data(), s.pixels.size());
        prevW = s.width;
        prevH = s.height;
        prevSlot = s.info.slotNs;
        havePrev = true;
        TimedFrame n;
        n.pixels.swap(s.pixels);
        n.width = s.width;
        n.height = s.height;
        n.dueNs = realDue;
        n.slotNs = s.info.slotNs;
        n.queuedNs = NowNs();
        n.kind = 'N';
        PresentTimed(n);
        ReturnBuffer(std::move(n.pixels));
    }
}

}  // namespace

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_FRAMEGEN");
        return v && std::strcmp(v, "1") == 0;
    }();
    return on;
}

void SubmitReal(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, const RealInfo& info) {
    static const bool started = [] {
        std::thread(Worker).detach();
        return true;
    }();
    (void)started;
    Submission s;
    s.pixels = TakeBuffer();
    s.pixels.swap(pixels);  // the command processor keeps a spare buffer for its next readback
    s.width = width;
    s.height = height;
    s.info = info;
    {
        std::lock_guard lk(g_mutex);
        g_queue.push_back(std::move(s));
        ++g_stats.submitted;
    }
    g_cv.notify_one();
}

void PrintStatus() {
    if (!Enabled()) return;
    std::string name;
    {
        std::lock_guard lk(g_mutex);
        name = g_generatorName;
    }
    const Stats s = TakeStats();
    uint64_t shownReal = 0, shownGen = 0, skipped = 0;
    TakePresentCounts(shownReal, shownGen, skipped);
    uint64_t dropped = 0;
    for (uint64_t n : s.dropped) dropped += n;
    const uint64_t attempts = s.generated + s.dropped[int(Drop::Generator)] + s.dropped[int(Drop::GenLate)];
    std::fprintf(stderr,
                 "[framegen] %s: %llu real in, %llu generated (%llu a refresh late), %llu dropped (first %llu, size %llu, "
                 "rate %llu, late %llu, steps %llu, cut %llu, generator %llu, gen-late %llu); gen %.2f ms avg, %.2f max; "
                 "cut diff %.1f avg, %.1f max; shown %llu real + %llu generated, %llu skipped\n",
                 name.c_str(), (unsigned long long)s.submitted, (unsigned long long)s.generated,
                 (unsigned long long)s.genLateShown, (unsigned long long)dropped,
                 (unsigned long long)s.dropped[int(Drop::First)], (unsigned long long)s.dropped[int(Drop::Size)],
                 (unsigned long long)s.dropped[int(Drop::Rate)], (unsigned long long)s.dropped[int(Drop::Late)],
                 (unsigned long long)s.dropped[int(Drop::Steps)], (unsigned long long)s.dropped[int(Drop::Cut)],
                 (unsigned long long)s.dropped[int(Drop::Generator)], (unsigned long long)s.dropped[int(Drop::GenLate)],
                 attempts ? s.genMsSum / double(attempts) : 0.0, s.genMsMax,
                 s.diffCount ? s.diffSum / double(s.diffCount) : 0.0, s.diffMax, (unsigned long long)shownReal,
                 (unsigned long long)shownGen, (unsigned long long)skipped);
}

std::unique_ptr<Generator> MakeGenerator(const char* name) {
    if (!name || std::strcmp(name, "blend") == 0) return std::make_unique<BlendGenerator>();
    if (std::strcmp(name, "repeat") == 0) return std::make_unique<RepeatGenerator>();
    if (std::strcmp(name, "warp") == 0) return MakeWarpGenerator(WarpParamsFromEnv());
    return nullptr;
}

const char* DropName(Drop d) {
    switch (d) {
    case Drop::None: return "none";
    case Drop::First: return "first";
    case Drop::Size: return "size";
    case Drop::Rate: return "rate";
    case Drop::Late: return "late";
    case Drop::Steps: return "steps";
    case Drop::Cut: return "cut";
    case Drop::Generator: return "generator";
    case Drop::GenLate: return "gen-late";
    default: return "?";
    }
}

Drop Decide(const DecideIn& in) {
    if (!in.havePrev) return Drop::First;
    if (!in.sameSize) return Drop::Size;
    // Only the game's 30 Hz: at its 60 Hz (the boot loading screen) there's nothing to fill in.
    if (in.periodNs < 25'000'000) return Drop::Rate;
    // A late frame or a load: the previous real frame is more than half a period off its beat.
    if (in.sinceLastSlotNs > in.periodNs + in.periodNs / 2 || in.sinceLastSlotNs < in.periodNs / 2) return Drop::Late;
    if (in.simSteps >= 0 && in.simSteps != 1) return Drop::Steps;
    if (in.cutDiff > in.cutThreshold) return Drop::Cut;
    return Drop::None;
}

double MeanAbsDiff(const HostFrame& a, const HostFrame& b) {
    if (!a.pixels || !b.pixels || a.width != b.width || a.height != b.height || a.width == 0 || a.height == 0) return 0;
    uint64_t sum = 0, n = 0;
    for (uint32_t y = 4; y < a.height; y += 8) {
        const uint8_t* ra = a.pixels + size_t(y) * a.width * 4;
        const uint8_t* rb = b.pixels + size_t(y) * b.width * 4;
        for (uint32_t x = 4; x < a.width; x += 8) {
            const uint8_t* pa = ra + size_t(x) * 4;
            const uint8_t* pb = rb + size_t(x) * 4;
            sum += uint64_t(std::abs(int(pa[0]) - int(pb[0]))) + uint64_t(std::abs(int(pa[1]) - int(pb[1]))) +
                   uint64_t(std::abs(int(pa[2]) - int(pb[2])));
            ++n;
        }
    }
    return n ? double(sum) / double(n * 3) : 0.0;
}

void Blend(const uint8_t* a, const uint8_t* b, uint8_t* out, size_t bytes) {
    size_t i = 0;
    for (; i + 64 <= bytes; i += 64) {
        for (size_t k = 0; k < 64; k += 16) {
            const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a + i + k));
            const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b + i + k));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i + k), _mm_avg_epu8(va, vb));
        }
    }
    for (; i < bytes; ++i) out[i] = static_cast<uint8_t>((unsigned(a[i]) + unsigned(b[i]) + 1) >> 1);
}

int64_t DelayNs(double refreshHz, double overrideMs) {
    if (overrideMs >= 0) return static_cast<int64_t>(overrideMs * 1e6);
    const double hz = refreshHz > 0 ? refreshHz : 60.0;
    return static_cast<int64_t>(1e9 / hz) + 4'000'000;
}

Stats TakeStats() {
    std::lock_guard lk(g_mutex);
    Stats s = g_stats;
    g_stats = Stats{};
    return s;
}

}  // namespace video::framegen
