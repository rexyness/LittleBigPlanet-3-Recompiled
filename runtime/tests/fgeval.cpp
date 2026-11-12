// Frame generation offline evaluator (perf/fg_warp.md in the planning repo).
//
// Runs the generators on a dump set from LBP3_FG_DUMP (analysis/fg_dump/<set>: fNNN_final.raw,
// RGBA8 1920x1080, plus the manifest's per-frame "sim" counter) and:
//  - skip-one score: the midpoint of frames k-1 and k+1 against the real frame k (PSNR over the
//    whole frame and over the moving pixels, mean absolute error, generation time), for every k
//    whose neighbours are one simulation step away on each side;
//  - stills: for the chosen frames, the real in-between frame G(k, k+1) next to k and k+1, and the
//    skip-one result next to the truth with an error map, as PNG.
//
//   fgeval <set dir> [--gens warp,blend,repeat] [--out <dir>] [--stills k,k,...] [--from k] [--to k]
//          [--image final|hudless] [--quiet]
// Warp parameters come from the LBP3_FRAMEGEN_WARP_* environment (framegen_warp.h).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "video/framegen.h"
#include "video/framegen_gpu.h"
#include "video/framegen_warp.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fg = video::framegen;

namespace {

constexpr uint32_t kW = 1920, kH = 1080;

bool LoadRaw(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.resize(size_t(kW) * kH * 4);
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    return size_t(f.gcount()) == out.size();
}

std::vector<long long> ManifestSims(const std::string& dir) {
    std::ifstream f(dir + "/manifest.json");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    std::vector<long long> sims;
    size_t pos = 0;
    while ((pos = s.find("\"sim\":", pos)) != std::string::npos) {
        pos += 6;
        sims.push_back(std::atoll(s.c_str() + pos));
    }
    return sims;
}

std::string Frame(const std::string& dir, int k, const std::string& kind) {
    char name[64];
    std::snprintf(name, sizeof name, "/f%03d_%s.raw", k, kind.c_str());
    return dir + name;
}

struct Score {
    double mseAll = 0, mseMoving = 0, maeMoving = 0;
    uint64_t moving = 0;
};

// Over R, G, B. Moving = pixels whose k-1 and k+1 differ by more than 10 in some channel.
Score Compare(const std::vector<uint8_t>& gen, const std::vector<uint8_t>& truth, const std::vector<uint8_t>& a,
              const std::vector<uint8_t>& b) {
    Score s;
    double sumAll = 0, sumMov = 0, absMov = 0;
    const size_t n = size_t(kW) * kH;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* g = &gen[i * 4];
        const uint8_t* t = &truth[i * 4];
        const uint8_t* pa = &a[i * 4];
        const uint8_t* pb = &b[i * 4];
        double e = 0, ae = 0;
        bool mov = false;
        for (int c = 0; c < 3; ++c) {
            const double d = double(g[c]) - double(t[c]);
            e += d * d;
            ae += std::fabs(d);
            mov |= std::abs(int(pa[c]) - int(pb[c])) > 10;
        }
        sumAll += e;
        if (mov) {
            sumMov += e;
            absMov += ae;
            ++s.moving;
        }
    }
    s.mseAll = sumAll / double(n * 3);
    s.mseMoving = s.moving ? sumMov / double(s.moving * 3) : 0;
    s.maeMoving = s.moving ? absMov / double(s.moving * 3) : 0;
    return s;
}

double Psnr(double mse) { return mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0; }

// Writes images side by side (each RGBA8 kW x kH), downscaled by 2, as an RGB PNG.
void WriteRow(const std::string& path, const std::vector<const std::vector<uint8_t>*>& imgs) {
    const int w = kW / 2, h = kH / 2, n = int(imgs.size());
    std::vector<uint8_t> out(size_t(w) * n * h * 3);
    for (int i = 0; i < n; ++i) {
        const auto& src = *imgs[i];
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 3; ++c) {
                    const int s = src[(size_t(2 * y) * kW + 2 * x) * 4 + c] + src[(size_t(2 * y) * kW + 2 * x + 1) * 4 + c] +
                                  src[(size_t(2 * y + 1) * kW + 2 * x) * 4 + c] +
                                  src[(size_t(2 * y + 1) * kW + 2 * x + 1) * 4 + c];
                    out[(size_t(y) * w * n + size_t(i) * w + x) * 3 + c] = uint8_t((s + 2) / 4);
                }
            }
    }
    stbi_write_png(path.c_str(), w * n, h, 3, out.data(), w * n * 3);
}

// Full-resolution crop (x0, y0, cw, ch) of each image, side by side, as an RGB PNG.
void WriteCrop(const std::string& path, const std::vector<const std::vector<uint8_t>*>& imgs, int x0, int y0, int cw,
               int ch) {
    const int n = int(imgs.size());
    std::vector<uint8_t> out(size_t(cw) * n * ch * 3);
    for (int i = 0; i < n; ++i)
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                for (int c = 0; c < 3; ++c)
                    out[(size_t(y) * cw * n + size_t(i) * cw + x) * 3 + c] =
                        (*imgs[i])[(size_t(y0 + y) * kW + x0 + x) * 4 + c];
    stbi_write_png(path.c_str(), cw * n, ch, 3, out.data(), cw * n * 3);
}

std::vector<uint8_t> ErrorMap(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<uint8_t> out(a.size());
    for (size_t i = 0; i < a.size(); i += 4) {
        int d = 0;
        for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(a[i + c]) - int(b[i + c])));
        const uint8_t v = uint8_t(std::min(255, d * 4));
        out[i] = v;
        out[i + 1] = v;
        out[i + 2] = v;
        out[i + 3] = 255;
    }
    return out;
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(item);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: fgeval <set dir> [--gens warp,blend,repeat] [--out dir] [--stills k,..] "
                             "[--from k] [--to k] [--image final|hudless] [--crop x,y,w,h] [--novec] [--nohud] [--quiet]\n");
        return 2;
    }
    const std::string dir = argv[1];
    std::string gens = "warp,blend,repeat", outDir, stills, image = "final", crop;
    int from = 1, to = 1 << 30;
    bool quiet = false, useVec = true, useHud = true, compare = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--gens") gens = next();
        else if (a == "--out") outDir = next();
        else if (a == "--stills") stills = next();
        else if (a == "--from") from = std::atoi(next().c_str());
        else if (a == "--to") to = std::atoi(next().c_str());
        else if (a == "--image") image = next();
        else if (a == "--crop") crop = next();
        else if (a == "--quiet") quiet = true;
        else if (a == "--novec") useVec = false;
        else if (a == "--nohud") useHud = false;
        else if (a == "--compare") compare = true;  // byte-compare "warp" (CPU) with "warp-gpu" per triple
    }
    const std::vector<long long> sims = ManifestSims(dir);
    if (sims.size() < 3) {
        std::fprintf(stderr, "fgeval: no manifest sims in %s\n", dir.c_str());
        return 2;
    }
    const int n = int(sims.size());
    std::vector<std::unique_ptr<fg::Generator>> gs;
    for (const auto& name : Split(gens)) {
        auto g = fg::MakeGenerator(name.c_str());
        if (!g) {
            std::fprintf(stderr, "fgeval: unknown generator %s\n", name.c_str());
            return 2;
        }
        gs.push_back(std::move(g));
    }

    // Skip-one scores.
    std::vector<double> sumAll(gs.size()), sumMov(gs.size()), sumMae(gs.size()), sumMs(gs.size()), maxMs(gs.size());
    std::vector<int> declined(gs.size());
    std::vector<double> gpuSum(gs.size()), gpuMax(gs.size());
    std::vector<int> gpuN(gs.size());
    std::vector<std::vector<uint8_t>> outs(gs.size());
    std::vector<bool> outDeclined(gs.size());
    int cpuIdx = -1, gpuIdx = -1;
    for (size_t gi = 0; gi < gs.size(); ++gi) {
        if (std::strcmp(gs[gi]->Name(), "warp") == 0) cpuIdx = int(gi);
        if (std::strcmp(gs[gi]->Name(), "warp-gpu") == 0) gpuIdx = int(gi);
    }
    uint64_t cmpTriples = 0, cmpIdentical = 0, cmpDiffPixels = 0, cmpDeclineMismatch = 0;
    int cmpMaxDiff = 0;
    int scored = 0, skipped = 0;
    std::vector<uint8_t> a, t, b, out, vel, hlA, hlB;
    for (int k = std::max(1, from); k < std::min(n - 1, to + 1); ++k) {
        if (sims[k] - sims[k - 1] != 1 || sims[k + 1] - sims[k] != 1) {
            ++skipped;
            continue;
        }
        if (!LoadRaw(Frame(dir, k - 1, image), a) || !LoadRaw(Frame(dir, k, image), t) ||
            !LoadRaw(Frame(dir, k + 1, image), b)) {
            ++skipped;
            continue;
        }
        const bool haveVel = useVec && LoadRaw(Frame(dir, k + 1, "velocity"), vel);
        const bool haveHud = useHud && LoadRaw(Frame(dir, k - 1, "hudless"), hlA) && LoadRaw(Frame(dir, k + 1, "hudless"), hlB);
        if (!quiet) std::printf("k=%3d", k);
        for (size_t gi = 0; gi < gs.size(); ++gi) {
            fg::GenInputs in;
            in.prev = {a.data(), kW, kH};
            in.cur = {b.data(), kW, kH};
            in.t = 0.5f;
            if (haveVel) in.motionVectors = vel.data(), in.motionScale = 2.0f;
            if (haveHud) in.hudlessPrev = hlA.data(), in.hudless = hlB.data();
            const auto t0 = std::chrono::steady_clock::now();
            bool ok = gs[gi]->Generate(in, out);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            outDeclined[gi] = !ok;
            if (!ok) {
                // Declined: the window keeps showing N-1 (scored as such).
                ++declined[gi];
                out = a;
                ok = true;
            }
            if (compare) outs[gi] = out;
            double gpuMs = -1;
            if (const auto* gr = fg::LastGpuResult(gs[gi].get()); gr && gr->gpuMs >= 0) {
                gpuMs = gr->gpuMs;
                gpuSum[gi] += gpuMs;
                gpuMax[gi] = std::max(gpuMax[gi], gpuMs);
                ++gpuN[gi];
            }
            const Score s = Compare(out, t, a, b);
            sumAll[gi] += Psnr(s.mseAll);
            sumMov[gi] += Psnr(s.mseMoving);
            sumMae[gi] += s.maeMoving;
            sumMs[gi] += ms;
            maxMs[gi] = std::max(maxMs[gi], ms);
            if (!quiet) {
                std::printf("  %s %.2f/%.2f dB %.1f ms", gs[gi]->Name(), Psnr(s.mseAll), Psnr(s.mseMoving), ms);
                if (gpuMs >= 0) std::printf(" (gpu %.3f ms)", gpuMs);
                if (const fg::WarpDebug* d = fg::LastWarpDebug(gs[gi].get()))
                    std::printf(" [luma %.1f search %.1f compose %.1f; static %.0f%% alt %.1f%% fall %.1f%% hud %.1f%%; vec cov %.0f%% sat %.0f%%%s]",
                                d->msLuma, d->msSearch, d->msCompose, 100.0 * d->pixStatic / (kW * kH),
                                100.0 * d->pixAlt / (kW * kH), 100.0 * d->pixFallback / (kW * kH),
                                100.0 * d->pixHud / (kW * kH), 100.0 * d->vecCovered, 100.0 * d->vecSaturated,
                                d->declined ? " DECLINED" : "");
            }
        }
        if (compare && cpuIdx >= 0 && gpuIdx >= 0) {
            ++cmpTriples;
            const auto& c = outs[cpuIdx];
            const auto& g = outs[gpuIdx];
            uint64_t diffPx = 0;
            int maxD = 0;
            for (size_t i = 0; i + 3 < c.size() && i + 3 < g.size(); i += 4) {
                int d = 0;
                for (int ch = 0; ch < 4; ++ch) d = std::max(d, std::abs(int(c[i + ch]) - int(g[i + ch])));
                if (d) ++diffPx, maxD = std::max(maxD, d);
            }
            if (outDeclined[cpuIdx] != outDeclined[gpuIdx]) ++cmpDeclineMismatch;
            if (!diffPx && c.size() == g.size()) ++cmpIdentical;
            cmpDiffPixels += diffPx;
            cmpMaxDiff = std::max(cmpMaxDiff, maxD);
            if (!quiet) std::printf("  [cpu/gpu: %llu px differ, max %d%s]", (unsigned long long)diffPx, maxD,
                                    outDeclined[cpuIdx] != outDeclined[gpuIdx] ? ", DECLINE MISMATCH" : "");
        }
        if (!quiet) std::printf("  moving %.1f%%\n", 100.0 * Compare(a, b, a, b).moving / (kW * kH));
        ++scored;
    }
    std::printf("\n%s: %d triples scored, %d skipped (steps != 1 or missing)\n", dir.c_str(), scored, skipped);
    for (size_t gi = 0; gi < gs.size() && scored; ++gi)
        std::printf("  %-7s PSNR all %.2f dB, moving %.2f dB, MAE moving %.2f; %.2f ms avg, %.2f max; %d declined\n",
                    gs[gi]->Name(), sumAll[gi] / scored, sumMov[gi] / scored, sumMae[gi] / scored, sumMs[gi] / scored,
                    maxMs[gi], declined[gi]);
    for (size_t gi = 0; gi < gs.size(); ++gi)
        if (gpuN[gi])
            std::printf("  %-7s GPU time %.3f ms avg, %.3f max (%d timed)\n", gs[gi]->Name(), gpuSum[gi] / gpuN[gi],
                        gpuMax[gi], gpuN[gi]);
    if (cmpTriples)
        std::printf("  cpu/gpu compare: %llu of %llu triples identical; %llu differing pixels in all; max diff %d; "
                    "%llu decline mismatches\n",
                    (unsigned long long)cmpIdentical, (unsigned long long)cmpTriples,
                    (unsigned long long)cmpDiffPixels, cmpMaxDiff, (unsigned long long)cmpDeclineMismatch);

    // Stills.
    if (!outDir.empty() && !stills.empty()) {
        int cx = 0, cy = 0, cw = 0, ch = 0;
        if (!crop.empty()) std::sscanf(crop.c_str(), "%d,%d,%d,%d", &cx, &cy, &cw, &ch);
        const std::string setName = dir.substr(dir.find_last_of("/\\") + 1);
        for (const auto& ks : Split(stills)) {
            const int k = std::atoi(ks.c_str());
            if (k < 1 || k + 1 >= n) continue;
            std::vector<uint8_t> f0, f1, f2, v2, h0, h1, h2;
            const bool sv = useVec && LoadRaw(Frame(dir, k + 1, "velocity"), v2);
            const bool sh = useHud && LoadRaw(Frame(dir, k - 1, "hudless"), h0) && LoadRaw(Frame(dir, k, "hudless"), h1) &&
                            LoadRaw(Frame(dir, k + 1, "hudless"), h2);
            if (!LoadRaw(Frame(dir, k - 1, image), f0) || !LoadRaw(Frame(dir, k, image), f1) ||
                !LoadRaw(Frame(dir, k + 1, image), f2))
                continue;
            for (size_t gi = 0; gi < gs.size(); ++gi) {
                // The real use: the in-between of k and k+1.
                fg::GenInputs in;
                in.prev = {f1.data(), kW, kH};
                in.cur = {f2.data(), kW, kH};
                if (sv) in.motionVectors = v2.data();
                if (sh) in.hudlessPrev = h1.data(), in.hudless = h2.data();
                std::vector<uint8_t> g;
                if (!gs[gi]->Generate(in, g)) g = f1;
                char base[256];
                std::snprintf(base, sizeof base, "%s/%s_k%03d_%s", outDir.c_str(), setName.c_str(), k, gs[gi]->Name());
                WriteRow(std::string(base) + "_mid.png", {&f1, &g, &f2});
                if (const fg::WarpDebug* d = fg::LastWarpDebug(gs[gi].get())) {
                    // The block field: R/G = 128 + 4 * half-step x/y (full-res px), B = cost / 16; 8x per block.
                    const int bw = int(d->blocksX), bh = int(d->blocksY), z = 8;
                    std::vector<uint8_t> img(size_t(bw) * z * bh * z * 3);
                    for (int y = 0; y < bh * z; ++y)
                        for (int x = 0; x < bw * z; ++x) {
                            const size_t i = size_t(y / z) * bw + x / z;
                            uint8_t* o = &img[(size_t(y) * bw * z + x) * 3];
                            o[0] = uint8_t(std::clamp(128 + int(4 * d->hx[i]), 0, 255));
                            o[1] = uint8_t(std::clamp(128 + int(4 * d->hy[i]), 0, 255));
                            o[2] = uint8_t(std::clamp(int(d->cost[i] / 16), 0, 255));
                        }
                    stbi_write_png((std::string(base) + "_field.png").c_str(), bw * z, bh * z, 3, img.data(), bw * z * 3);
                    // And a text histogram of the half-step x for a quick look.
                    std::printf("%s k=%d field: hx range %.0f..%.0f, hy range %.0f..%.0f\n", gs[gi]->Name(), k,
                                *std::min_element(d->hx.begin(), d->hx.end()), *std::max_element(d->hx.begin(), d->hx.end()),
                                *std::min_element(d->hy.begin(), d->hy.end()), *std::max_element(d->hy.begin(), d->hy.end()));
                }
                if (cw > 0) WriteCrop(std::string(base) + "_mid_crop.png", {&f1, &g, &f2}, cx, cy, cw, ch);
                // Skip-one: generated vs the real k, with the error map.
                in.prev = {f0.data(), kW, kH};
                in.cur = {f2.data(), kW, kH};
                if (sv) in.motionVectors = v2.data(), in.motionScale = 2.0f;
                if (sh) in.hudlessPrev = h0.data(), in.hudless = h2.data();
                std::vector<uint8_t> s1;
                if (!gs[gi]->Generate(in, s1)) s1 = f0;
                const std::vector<uint8_t> err = ErrorMap(s1, f1);
                WriteRow(std::string(base) + "_skip.png", {&s1, &f1, &err});
                if (cw > 0) WriteCrop(std::string(base) + "_skip_crop.png", {&s1, &f1, &err}, cx, cy, cw, ch);
            }
        }
        std::printf("stills written to %s\n", outDir.c_str());
    }
    return 0;
}
