#include "video/framegen_warp.h"

#include <emmintrin.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>

namespace video::framegen {
namespace {

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int EnvInt(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    char* end = nullptr;
    const long n = std::strtol(v, &end, 10);
    return end != v ? int(n) : fallback;
}
float EnvFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    char* end = nullptr;
    const double d = std::strtod(v, &end);
    return end != v ? float(d) : fallback;
}

// A small fixed pool: Run(n, fn) calls fn(0..n-1) across the workers and the caller, and
// returns when all are done.
class Pool {
public:
    explicit Pool(int n) {
        for (int i = 0; i < n - 1; ++i) threads_.emplace_back([this] { Loop(); });
    }
    ~Pool() {
        {
            std::lock_guard lk(m_);
            quit_ = true;
            ++gen_;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }
    void Run(int tasks, const std::function<void(int)>& fn) {
        if (tasks <= 0) return;
        if (threads_.empty() || tasks == 1) {
            for (int i = 0; i < tasks; ++i) fn(i);
            return;
        }
        {
            std::lock_guard lk(m_);
            fn_ = &fn;
            tasks_ = tasks;
            next_.store(0);
            pending_ = tasks;
            ++gen_;
        }
        cv_.notify_all();
        Work(fn, tasks);
        // Wait for the tasks and for every worker to have left Work(), so that a late worker can't
        // take an index of the next Run.
        std::unique_lock lk(m_);
        done_.wait(lk, [this] { return pending_ == 0 && active_ == 0; });
        fn_ = nullptr;
    }

private:
    void Work(const std::function<void(int)>& fn, int tasks) {
        for (;;) {
            const int i = next_.fetch_add(1);
            if (i >= tasks) return;
            fn(i);
            std::lock_guard lk(m_);
            if (--pending_ == 0) done_.notify_all();
        }
    }
    void Loop() {
        uint64_t seen = 0;
        for (;;) {
            const std::function<void(int)>* fn = nullptr;
            int tasks = 0;
            {
                std::unique_lock lk(m_);
                cv_.wait(lk, [&] { return gen_ != seen; });
                seen = gen_;
                if (quit_) return;
                if (!fn_) continue;
                fn = fn_;
                tasks = tasks_;
                ++active_;
            }
            Work(*fn, tasks);
            std::lock_guard lk(m_);
            if (--active_ == 0) done_.notify_all();
        }
    }
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    const std::function<void(int)>* fn_ = nullptr;
    int tasks_ = 0;
    int pending_ = 0;
    int active_ = 0;
    std::atomic<int> next_{0};
    uint64_t gen_ = 0;
    bool quit_ = false;
};

// A luma plane with an edge-replicated border, so block reads at any searched offset are safe.
struct Plane {
    int w = 0, h = 0, pad = 0, stride = 0;
    std::vector<uint8_t> buf;
    void Alloc(int w_, int h_, int pad_) {
        w = w_;
        h = h_;
        pad = pad_;
        stride = w + 2 * pad;
        buf.resize(size_t(stride) * (h + 2 * pad));
    }
    uint8_t* Row(int y) { return buf.data() + size_t(y + pad) * stride + pad; }
    const uint8_t* Row(int y) const { return buf.data() + size_t(y + pad) * stride + pad; }
    void PadEdges() {
        for (int y = 0; y < h; ++y) {
            uint8_t* r = Row(y);
            std::memset(r - pad, r[0], pad);
            std::memset(r + w, r[w - 1], pad);
        }
        const uint8_t* top = Row(0) - pad;
        const uint8_t* bottom = Row(h - 1) - pad;
        for (int y = 1; y <= pad; ++y) {
            std::memcpy(Row(-y) - pad, top, stride);
            std::memcpy(Row(h - 1 + y) - pad, bottom, stride);
        }
    }
};

constexpr int kLevels = 3;   // 1/2, 1/4, 1/8 of the frame
constexpr int kBlock = 8;    // block size at every level
constexpr int kPad = 96;     // >= kBlock + the largest offset searched at any level

struct Vec {
    int16_t x = 0, y = 0;
};
inline bool operator==(Vec a, Vec b) { return a.x == b.x && a.y == b.y; }

struct Field {
    int bw = 0, bh = 0;
    std::vector<Vec> v;
    std::vector<uint32_t> cost;
    void Alloc(int w, int h) {
        bw = (w + kBlock - 1) / kBlock;
        bh = (h + kBlock - 1) / kBlock;
        v.assign(size_t(bw) * bh, Vec{});
        cost.assign(size_t(bw) * bh, 0);
    }
    Vec& At(int x, int y) { return v[size_t(y) * bw + x]; }
    Vec At(int x, int y) const { return v[size_t(y) * bw + x]; }
};

// Sum of absolute differences of the 8x8 blocks of a at (ax, ay) and b at (bx, by).
inline uint32_t Sad8At(const Plane& a, int ax, int ay, const Plane& b, int bx, int by) {
    const uint8_t* pa = a.Row(ay) + ax;
    const uint8_t* pb = b.Row(by) + bx;
    __m128i acc = _mm_setzero_si128();
    for (int r = 0; r < kBlock; r += 2) {
        const __m128i ra = _mm_unpacklo_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(pa)),
                                              _mm_loadl_epi64(reinterpret_cast<const __m128i*>(pa + a.stride)));
        const __m128i rb = _mm_unpacklo_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(pb)),
                                              _mm_loadl_epi64(reinterpret_cast<const __m128i*>(pb + b.stride)));
        acc = _mm_add_epi64(acc, _mm_sad_epu8(ra, rb));
        pa += 2 * a.stride;
        pb += 2 * b.stride;
    }
    return uint32_t(_mm_cvtsi128_si32(acc) + _mm_cvtsi128_si32(_mm_srli_si128(acc, 8)));
}

class WarpGenerator final : public Generator {
public:
    explicit WarpGenerator(const WarpParams& p) : p_(p), pool_(std::max(1, p.threads)) {}
    const char* Name() const override { return "warp"; }
    const WarpDebug& Debug() const { return dbg_; }

    bool Generate(const GenInputs& in, std::vector<uint8_t>& out) override {
        const uint32_t W = in.cur.width, H = in.cur.height;
        if (!in.prev.pixels || !in.cur.pixels || in.prev.width != W || in.prev.height != H || W < 64 || H < 64)
            return false;
        out.resize(size_t(W) * H * 4);
        const double t0 = NowMs();
        BuildPyramid(in.prev, prevPyr_);
        BuildPyramid(in.cur, curPyr_);
        const double t1 = NowMs();
        Search(in);
        const double t2 = NowMs();
        Compose(in, out.data());
        const double t3 = NowMs();
        dbg_.msLuma = t1 - t0;
        dbg_.msSearch = t2 - t1;
        dbg_.msCompose = t3 - t2;
        const Field& f = mid_;
        dbg_.blocksX = f.bw;
        dbg_.blocksY = f.bh;
        dbg_.hx.resize(f.v.size());
        dbg_.hy.resize(f.v.size());
        for (size_t i = 0; i < f.v.size(); ++i) {
            dbg_.hx[i] = float(f.v[i].x);
            dbg_.hy[i] = float(f.v[i].y);
        }
        dbg_.cost = f.cost;
        return true;
    }

private:
    // Level 0 = half resolution luma, (R + 2G + B) / 4 over each 2x2.
    void BuildPyramid(const HostFrame& f, Plane (&pyr)[kLevels]) {
        const int w0 = int(f.width / 2), h0 = int(f.height / 2);
        pyr[0].Alloc(w0, h0, kPad);
        const int bands = 32;
        pool_.Run(bands, [&](int band) {
            const int y0 = h0 * band / bands, y1 = h0 * (band + 1) / bands;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* r0 = f.pixels + size_t(2 * y) * f.width * 4;
                const uint8_t* r1 = r0 + size_t(f.width) * 4;
                uint8_t* o = pyr[0].Row(y);
                for (int x = 0; x < w0; ++x) {
                    const uint8_t* a = r0 + x * 8;
                    const uint8_t* b = r1 + x * 8;
                    const unsigned s = a[0] + 2u * a[1] + a[2] + a[4] + 2u * a[5] + a[6] + b[0] + 2u * b[1] + b[2] +
                                       b[4] + 2u * b[5] + b[6];
                    o[x] = uint8_t((s + 8) >> 4);
                }
            }
        });
        pyr[0].PadEdges();
        for (int l = 1; l < kLevels; ++l) {
            const Plane& s = pyr[l - 1];
            Plane& d = pyr[l];
            d.Alloc(s.w / 2, s.h / 2, kPad);
            for (int y = 0; y < d.h; ++y) {
                const uint8_t* r0 = s.Row(2 * y);
                const uint8_t* r1 = s.Row(2 * y + 1);
                uint8_t* o = d.Row(y);
                for (int x = 0; x < d.w; ++x)
                    o[x] = uint8_t((r0[2 * x] + r0[2 * x + 1] + r1[2 * x] + r1[2 * x + 1] + 2) >> 2);
            }
            d.PadEdges();
        }
    }

    // Keeps a level-pixel offset inside what the padded planes allow.
    static Vec Clamp(Vec v) {
        const int lim = kPad - kBlock - 1;
        return Vec{int16_t(std::clamp<int>(v.x, -lim, lim)), int16_t(std::clamp<int>(v.y, -lim, lim))};
    }
    static int L1(Vec a, Vec b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

    // One-sided matching: the anchor frame's block at (bx, by) against the other frame at
    // + sign * m, where m is the motion from N-1 to N (sign -1: anchor N, other N-1; +1: the reverse).
    uint32_t CostOne(const Plane& anchor, const Plane& other, int sign, int bx, int by, Vec m, const Vec* med) const {
        const int x = bx * kBlock, y = by * kBlock;
        uint32_t c = Sad8At(anchor, x, y, other, x + sign * m.x, y + sign * m.y);
        c += uint32_t(p_.lambdaZero) * uint32_t(std::abs(m.x) + std::abs(m.y));
        if (med) c += uint32_t(p_.lambdaSmooth) * 8u * uint32_t(L1(m, *med));
        return c;
    }

    // The in-between frame's block at (bx, by) (level 0): N-1 at p - floor(m / 2) against N at
    // p + (m - floor(m / 2)), so the two offsets add up to the motion m.
    uint32_t CostMid(int bx, int by, Vec m, const Vec* med) const {
        const int x = bx * kBlock, y = by * kBlock;
        const int px = m.x >> 1, py = m.y >> 1;
        uint32_t c = Sad8At(prevPyr_[0], x - px, y - py, curPyr_[0], x + (m.x - px), y + (m.y - py));
        c += uint32_t(p_.lambdaZero) * uint32_t(std::abs(m.x) + std::abs(m.y));
        if (med) c += uint32_t(p_.lambdaSmooth) * 8u * uint32_t(L1(m, *med));
        return c;
    }

    // Neighbour-candidate passes: each block may take a neighbour's vector or the neighbourhood's
    // median, and pays for its distance from that median, so isolated outliers fall in line.
    template <typename CostFn>
    void SmoothField(Field& f, CostFn cost) {
        for (int pass = 0; pass < p_.smoothPasses; ++pass) {
            tmp_ = f.v;
            pool_.Run(f.bh, [&](int by) {
                int xs[9], ys[9];
                for (int bx = 0; bx < f.bw; ++bx) {
                    Vec cands[10];
                    int n = 0, k = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int qx = bx + dx, qy = by + dy;
                            if (qx < 0 || qy < 0 || qx >= f.bw || qy >= f.bh) continue;
                            const Vec q = tmp_[size_t(qy) * f.bw + qx];
                            xs[k] = q.x;
                            ys[k] = q.y;
                            ++k;
                            bool dup = false;
                            for (int i = 0; i < n; ++i) dup |= cands[i] == q;
                            if (!dup) cands[n++] = q;
                        }
                    std::nth_element(xs, xs + k / 2, xs + k);
                    std::nth_element(ys, ys + k / 2, ys + k);
                    const Vec med{int16_t(xs[k / 2]), int16_t(ys[k / 2])};
                    bool dup = false;
                    for (int i = 0; i < n; ++i) dup |= cands[i] == med;
                    if (!dup) cands[n++] = med;
                    Vec best = tmp_[size_t(by) * f.bw + bx];
                    uint32_t bestC = cost(bx, by, best, &med);
                    for (int i = 0; i < n; ++i) {
                        const uint32_t c = cost(bx, by, cands[i], &med);
                        if (c < bestC) bestC = c, best = cands[i];
                    }
                    f.v[size_t(by) * f.bw + bx] = best;
                    f.cost[size_t(by) * f.bw + bx] = cost(bx, by, best, nullptr);
                }
            });
        }
    }

    // One-sided motion field, coarse to fine (sign as in CostOne).
    void OneSided(const Plane (&anchor)[kLevels], const Plane (&other)[kLevels], int sign, Field (&f)[kLevels]) {
        for (int l = 0; l < kLevels; ++l) f[l].Alloc(anchor[l].w, anchor[l].h);
        {
            const int l = kLevels - 1;
            Field& fl = f[l];
            const int R = p_.coarseRadius * 2;  // total motion at the coarsest level
            pool_.Run(fl.bh, [&](int by) {
                for (int bx = 0; bx < fl.bw; ++bx) {
                    Vec best{};
                    uint32_t bestC = UINT32_MAX;
                    for (int my = -R; my <= R; ++my)
                        for (int mx = -R; mx <= R; ++mx) {
                            const Vec m{int16_t(mx), int16_t(my)};
                            const uint32_t c = CostOne(anchor[l], other[l], sign, bx, by, m, nullptr);
                            if (c < bestC) bestC = c, best = m;
                        }
                    if (!gameL2_.empty()) {
                        const Vec g = Clamp(gameL2_[size_t(by) * fl.bw + bx]);
                        const uint32_t c = CostOne(anchor[l], other[l], sign, bx, by, g, nullptr);
                        if (c < bestC) bestC = c, best = g;
                    }
                    fl.v[size_t(by) * fl.bw + bx] = best;
                    fl.cost[size_t(by) * fl.bw + bx] = bestC;
                }
            });
            SmoothField(fl, [&](int bx, int by, Vec m, const Vec* med) {
                return CostOne(anchor[l], other[l], sign, bx, by, m, med);
            });
        }
        for (int l = kLevels - 2; l >= 0; --l) {
            Field& fl = f[l];
            const Field& pf = f[l + 1];
            pool_.Run(fl.bh, [&](int by) {
                for (int bx = 0; bx < fl.bw; ++bx) {
                    Vec cands[8];
                    int n = 0;
                    const int px = std::min(bx / 2, pf.bw - 1), py = std::min(by / 2, pf.bh - 1);
                    static const int nb[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                    for (const auto& d : nb) {
                        const int qx = px + d[0], qy = py + d[1];
                        if (qx < 0 || qy < 0 || qx >= pf.bw || qy >= pf.bh) continue;
                        const Vec pv = pf.v[size_t(qy) * pf.bw + qx];
                        cands[n++] = Clamp(Vec{int16_t(pv.x * 2), int16_t(pv.y * 2)});
                    }
                    cands[n++] = Vec{};
                    if (l == 0 && !gameL0_.empty()) cands[n++] = Clamp(gameL0_[size_t(by) * fl.bw + bx]);
                    Vec best{};
                    uint32_t bestC = UINT32_MAX;
                    for (int i = 0; i < n; ++i) {
                        const uint32_t c = CostOne(anchor[l], other[l], sign, bx, by, cands[i], nullptr);
                        if (c < bestC) bestC = c, best = cands[i];
                    }
                    const Vec centre = best;
                    const int r = p_.refineRadius;
                    for (int dy = -r; dy <= r; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            if (!dx && !dy) continue;
                            const Vec m = Clamp(Vec{int16_t(centre.x + dx), int16_t(centre.y + dy)});
                            const uint32_t c = CostOne(anchor[l], other[l], sign, bx, by, m, nullptr);
                            if (c < bestC) bestC = c, best = m;
                        }
                    fl.v[size_t(by) * fl.bw + bx] = best;
                    fl.cost[size_t(by) * fl.bw + bx] = bestC;
                }
            });
            SmoothField(fl, [&](int bx, int by, Vec m, const Vec* med) {
                return CostOne(anchor[l], other[l], sign, bx, by, m, med);
            });
        }
    }

    // The in-between frame's field: each block picks, among the motions actually measured around
    // it (both one-sided fields at the block and its four neighbours, and no motion), the one whose
    // two ends agree best. Restricting it to measured motions is what keeps a thin object on a
    // plain background from being "matched away" (both ends landing on the background).
    void MidField() {
        const Field& b = back_[0];
        const Field& fw = fwd_[0];
        mid_.Alloc(prevPyr_[0].w, prevPyr_[0].h);
        pool_.Run(mid_.bh, [&](int by) {
            for (int bx = 0; bx < mid_.bw; ++bx) {
                Vec cands[12];
                int n = 0;
                auto add = [&](Vec v) {
                    v = Clamp(v);
                    for (int i = 0; i < n; ++i)
                        if (cands[i] == v) return;
                    cands[n++] = v;
                };
                static const int nb[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& d : nb) {
                    const int qx = bx + d[0], qy = by + d[1];
                    if (qx < 0 || qy < 0 || qx >= b.bw || qy >= b.bh) continue;
                    add(b.v[size_t(qy) * b.bw + qx]);
                    add(fw.v[size_t(qy) * fw.bw + qx]);
                }
                add(Vec{});
                Vec best{};
                uint32_t bestC = UINT32_MAX;
                for (int i = 0; i < n; ++i) {
                    const uint32_t c = CostMid(bx, by, cands[i], nullptr);
                    if (c < bestC) bestC = c, best = cands[i];
                }
                mid_.v[size_t(by) * mid_.bw + bx] = best;
                mid_.cost[size_t(by) * mid_.bw + bx] = bestC;
            }
        });
        SmoothField(mid_, [&](int bx, int by, Vec m, const Vec* med) { return CostMid(bx, by, m, med); });
    }

    void Search(const GenInputs& in) {
        GameCandidates(in);
        OneSided(curPyr_, prevPyr_, -1, back_);
        OneSided(prevPyr_, curPyr_, +1, fwd_);
        MidField();
    }

    // The game's motion vectors (total motion N-1 -> N in level pixels) as candidates, when
    // enabled and their decoding is configured.
    void GameCandidates(const GenInputs& in) {
        gameL0_.clear();
        gameL2_.clear();
        if (!p_.useGameVectors || !in.motionVectors || p_.vecScaleX == 0.0f || p_.vecScaleY == 0.0f) return;
        const uint8_t* mv = static_cast<const uint8_t*>(in.motionVectors);
        const uint32_t W = in.cur.width, H = in.cur.height;
        auto motion = [&](int x, int y, float& mx, float& my) {
            x = std::clamp<int>(x, 0, int(W) - 1);
            y = std::clamp<int>(y, 0, int(H) - 1);
            const uint8_t* q = mv + (size_t(y) * W + x) * 4;
            mx = (float(q[p_.vecChanX]) - p_.vecBias) * p_.vecScaleX;
            my = (float(q[p_.vecChanY]) - p_.vecBias) * p_.vecScaleY;
        };
        for (int l : {0, kLevels - 1}) {
            const int bw = (prevPyr_[l].w + kBlock - 1) / kBlock, bh = (prevPyr_[l].h + kBlock - 1) / kBlock;
            auto& dst = l == 0 ? gameL0_ : gameL2_;
            dst.resize(size_t(bw) * bh);
            const int scale = 2 << l;  // full-res pixels per level pixel
            for (int by = 0; by < bh; ++by)
                for (int bx = 0; bx < bw; ++bx) {
                    float mx, my;
                    motion((bx * kBlock + kBlock / 2) * scale, (by * kBlock + kBlock / 2) * scale, mx, my);
                    dst[size_t(by) * bw + bx] = Vec{int16_t(std::lround(mx / scale)), int16_t(std::lround(my / scale))};
                }
        }
    }

    // Bilinear sample of a BGRA frame at (x, y) in 1/256 pixel units, clamped to the frame:
    // the four channels at once, returned as 4 x int32 (0..255).
    static inline __m128i Sample(const HostFrame& f, int x256, int y256) {
        const int W = int(f.width), H = int(f.height);
        int ix = x256 >> 8, iy = y256 >> 8;
        int fx = x256 & 255, fy = y256 & 255;
        if (ix < 0) ix = 0, fx = 0;
        if (iy < 0) iy = 0, fy = 0;
        if (ix >= W - 1) ix = W - 2, fx = 256;
        if (iy >= H - 1) iy = H - 2, fy = 256;
        const uint8_t* p0 = f.pixels + (size_t(iy) * W + ix) * 4;
        const uint8_t* p1 = p0 + size_t(W) * 4;
        const __m128i z = _mm_setzero_si128();
        // [p00 c0..3, p01 c0..3] and the row below, as 16-bit.
        const __m128i r0 = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p0)), z);
        const __m128i r1 = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p1)), z);
        const __m128i wx = _mm_set_epi16(short(fx), short(fx), short(fx), short(fx), short(256 - fx), short(256 - fx),
                                         short(256 - fx), short(256 - fx));
        __m128i t = _mm_mullo_epi16(r0, wx);  // <= 255 * 256 per lane, as unsigned
        __m128i b = _mm_mullo_epi16(r1, wx);
        t = _mm_add_epi16(t, _mm_srli_si128(t, 8));  // p00 * (256 - fx) + p01 * fx, <= 65280
        b = _mm_add_epi16(b, _mm_srli_si128(b, 8));
        t = _mm_srli_epi16(t, 1);  // <= 32640: fits a signed madd
        b = _mm_srli_epi16(b, 1);
        const __m128i tb = _mm_unpacklo_epi16(t, b);  // [t0 b0 t1 b1 t2 b2 t3 b3]
        const __m128i wy = _mm_set1_epi32(int((uint32_t(fy) << 16) | uint32_t(256 - fy)));
        const __m128i s = _mm_madd_epi16(tb, wy);  // t * (256 - fy) + b * fy, 15 fraction bits
        return _mm_srli_epi32(_mm_add_epi32(s, _mm_set1_epi32(1 << 14)), 15);
    }

    static inline int MaxDiff3(__m128i a, __m128i b) {
        const __m128i d = _mm_sub_epi32(a, b);
        const __m128i m = _mm_srai_epi32(d, 31);
        const __m128i ad = _mm_sub_epi32(_mm_xor_si128(d, m), m);
        alignas(16) int v[4];
        _mm_store_si128(reinterpret_cast<__m128i*>(v), ad);
        return std::max(v[0], std::max(v[1], v[2]));
    }

    static inline void Store(uint8_t* op, __m128i v) {
        const __m128i p16 = _mm_packs_epi32(v, v);
        const __m128i p8 = _mm_packus_epi16(p16, p16);
        const int px = _mm_cvtsi128_si32(p8);
        std::memcpy(op, &px, 4);
    }

    void Compose(const GenInputs& in, uint8_t* out) {
        const Field& f = mid_;
        const int W = int(in.cur.width), H = int(in.cur.height);
        const int blockFull = kBlock * 2;  // 16 full-res pixels per level-0 block
        // The half step in full-res pixels (= the level-0 motion), in 1/256 pixels.
        fx256_.resize(f.v.size());
        fy256_.resize(f.v.size());
        for (size_t i = 0; i < f.v.size(); ++i) {
            fx256_[i] = f.v[i].x * 256;
            fy256_[i] = f.v[i].y * 256;
        }
        // Per column: the field's interpolation cell and weight (block centres at 16 i + 8).
        colI_.resize(W);
        colW_.resize(W);
        for (int x = 0; x < W; ++x) {
            const int g = (x * 2 + 1 - blockFull) * 128 / blockFull;  // (x + 0.5 - 8) / 16 in 1/256
            int i0 = g >> 8, w = g & 255;
            if (g < 0) i0 = 0, w = 0;
            if (i0 >= f.bw - 1) i0 = f.bw - 1, w = 0;
            colI_[x] = i0;
            colW_[x] = w;
        }
        // Prev samples at p - h * 2t, cur samples at p + h * 2(1 - t); in 1/256.
        const int tP = int(std::lround(in.t * 512.0f)), tC = 512 - tP;
        const int wt = int(std::lround(in.t * 256.0f));
        const __m128i wPrev = _mm_set1_epi32(256 - wt), wCur = _mm_set1_epi32(wt), half = _mm_set1_epi32(128);
        const int bands = 64;
        std::atomic<uint64_t> nAvg{0}, nAlt{0}, nFall{0}, nStatic{0};
        pool_.Run(bands, [&](int band) {
            uint64_t cAvg = 0, cAlt = 0, cFall = 0, cStatic = 0;
            std::vector<int> rowX(f.bw), rowY(f.bw);
            const int y0 = H * band / bands, y1 = H * (band + 1) / bands;
            for (int y = y0; y < y1; ++y) {
                const int g = (y * 2 + 1 - blockFull) * 128 / blockFull;
                int j0 = g >> 8, wy = g & 255;
                if (g < 0) j0 = 0, wy = 0;
                if (j0 >= f.bh - 1) j0 = f.bh - 1, wy = 0;
                const int j1 = std::min(j0 + 1, f.bh - 1);
                for (int i = 0; i < f.bw; ++i) {
                    const size_t a0 = size_t(j0) * f.bw + i, a1 = size_t(j1) * f.bw + i;
                    rowX[i] = (fx256_[a0] * (256 - wy) + fx256_[a1] * wy) >> 8;
                    rowY[i] = (fy256_[a0] * (256 - wy) + fy256_[a1] * wy) >> 8;
                }
                const uint8_t* prow = in.prev.pixels + size_t(y) * W * 4;
                const uint8_t* crow = in.cur.pixels + size_t(y) * W * 4;
                uint8_t* orow = out + size_t(y) * W * 4;
                const int bj = std::min(y / blockFull, f.bh - 1);
                for (int x = 0; x < W; ++x) {
                    const uint8_t* pp = prow + x * 4;
                    const uint8_t* cp = crow + x * 4;
                    uint8_t* op = orow + x * 4;
                    if (p_.staticCopy) {
                        const int sd = std::max(std::abs(pp[0] - cp[0]),
                                                std::max(std::abs(pp[1] - cp[1]), std::abs(pp[2] - cp[2])));
                        if (sd <= p_.staticT) {
                            std::memcpy(op, cp, 4);
                            ++cStatic;
                            continue;
                        }
                    }
                    const int i0 = colI_[x], wx = colW_[x], i1 = std::min(i0 + 1, f.bw - 1);
                    const int hx = (rowX[i0] * (256 - wx) + rowX[i1] * wx) >> 8;
                    const int hy = (rowY[i0] * (256 - wx) + rowY[i1] * wx) >> 8;
                    __m128i a = Sample(in.prev, (x << 8) - ((hx * tP) >> 8), (y << 8) - ((hy * tP) >> 8));
                    __m128i b = Sample(in.cur, (x << 8) + ((hx * tC) >> 8), (y << 8) + ((hy * tC) >> 8));
                    int d = MaxDiff3(a, b);
                    if (d > p_.agreeT && p_.perPixelAlt) {
                        // Try the vectors of the 3x3 blocks around this pixel's block.
                        const int bi = std::min(x / blockFull, f.bw - 1);
                        int bestD = d;
                        __m128i ba = a, bb = b;
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di) {
                                const int qi = bi + di, qj = bj + dj;
                                if (qi < 0 || qj < 0 || qi >= f.bw || qj >= f.bh) continue;
                                const size_t q = size_t(qj) * f.bw + qi;
                                const int vx = fx256_[q], vy = fy256_[q];
                                const __m128i ta =
                                    Sample(in.prev, (x << 8) - ((vx * tP) >> 8), (y << 8) - ((vy * tP) >> 8));
                                const __m128i tb =
                                    Sample(in.cur, (x << 8) + ((vx * tC) >> 8), (y << 8) + ((vy * tC) >> 8));
                                const int td = MaxDiff3(ta, tb);
                                if (td < bestD) bestD = td, ba = ta, bb = tb;
                            }
                        if (bestD < d) {
                            ++cAlt;
                            d = bestD;
                            a = ba;
                            b = bb;
                        }
                    }
                    if (d > p_.altT && p_.fallback != 0) {
                        Store(op, (p_.fallback == 2 || in.t >= 0.5f) ? b : a);
                        ++cFall;
                        continue;
                    }
                    // Lanes hold 0..255 in their low 16 bits, so a 16-bit multiply is exact.
                    const __m128i mix = _mm_srli_epi32(
                        _mm_add_epi32(_mm_add_epi32(_mm_mullo_epi16(a, wPrev), _mm_mullo_epi16(b, wCur)), half), 8);
                    Store(op, mix);
                    ++cAvg;
                }
            }
            nAvg += cAvg;
            nAlt += cAlt;
            nFall += cFall;
            nStatic += cStatic;
        });
        dbg_.pixAveraged = nAvg;
        dbg_.pixAlt = nAlt;
        dbg_.pixFallback = nFall;
        dbg_.pixStatic = nStatic;
    }

    WarpParams p_;
    Pool pool_;
    Plane prevPyr_[kLevels], curPyr_[kLevels];
    Field back_[kLevels], fwd_[kLevels], mid_;
    std::vector<Vec> tmp_, gameL0_, gameL2_;
    std::vector<int> fx256_, fy256_, colI_, colW_;
    WarpDebug dbg_;
};

}  // namespace

WarpParams WarpParamsFromEnv() {
    WarpParams p;
    p.threads = EnvInt("LBP3_FRAMEGEN_WARP_THREADS", p.threads);
    p.coarseRadius = EnvInt("LBP3_FRAMEGEN_WARP_RADIUS", p.coarseRadius);
    p.refineRadius = EnvInt("LBP3_FRAMEGEN_WARP_REFINE", p.refineRadius);
    p.smoothPasses = EnvInt("LBP3_FRAMEGEN_WARP_SMOOTH", p.smoothPasses);
    p.lambdaSmooth = EnvInt("LBP3_FRAMEGEN_WARP_LSMOOTH", p.lambdaSmooth);
    p.lambdaZero = EnvInt("LBP3_FRAMEGEN_WARP_LZERO", p.lambdaZero);
    p.agreeT = EnvInt("LBP3_FRAMEGEN_WARP_AGREE", p.agreeT);
    p.altT = EnvInt("LBP3_FRAMEGEN_WARP_ALT", p.altT);
    p.staticT = EnvInt("LBP3_FRAMEGEN_WARP_STATIC", p.staticT);
    p.perPixelAlt = EnvInt("LBP3_FRAMEGEN_WARP_PIXALT", p.perPixelAlt ? 1 : 0) != 0;
    p.staticCopy = EnvInt("LBP3_FRAMEGEN_WARP_STATICCOPY", p.staticCopy ? 1 : 0) != 0;
    p.fallback = EnvInt("LBP3_FRAMEGEN_WARP_FALLBACK", p.fallback);
    p.useGameVectors = EnvInt("LBP3_FRAMEGEN_WARP_VEC", 0) != 0;
    p.vecBias = EnvFloat("LBP3_FRAMEGEN_WARP_VEC_BIAS", p.vecBias);
    p.vecScaleX = EnvFloat("LBP3_FRAMEGEN_WARP_VEC_SX", p.vecScaleX);
    p.vecScaleY = EnvFloat("LBP3_FRAMEGEN_WARP_VEC_SY", p.vecScaleY);
    p.vecChanX = EnvInt("LBP3_FRAMEGEN_WARP_VEC_CX", p.vecChanX);
    p.vecChanY = EnvInt("LBP3_FRAMEGEN_WARP_VEC_CY", p.vecChanY);
    return p;
}

std::unique_ptr<Generator> MakeWarpGenerator(const WarpParams& p) { return std::make_unique<WarpGenerator>(p); }

const WarpDebug* LastWarpDebug(const Generator* g) {
    const auto* w = dynamic_cast<const WarpGenerator*>(g);
    return w ? &w->Debug() : nullptr;
}

}  // namespace video::framegen
