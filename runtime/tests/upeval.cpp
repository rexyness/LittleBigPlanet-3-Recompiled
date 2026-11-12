// The window's upscaler on dumped frames (analysis/fg_dump; perf/upscale.md in the planning repo):
// bilinear (what the window's stretch does) against FSR 1 at chosen RCAS sharpness values.
//
//   upeval <frame.raw> <out dir> [--size WxH] [--sharp a,b,...] [--crop name:x,y,w,h ...]
//
// frame.raw: 1920x1080, 4 bytes per pixel as the game stores it (R, G, B, A in the dumps). Writes
// <out>/<stem>_bilinear.png and <stem>_fsr<s>.png (full size), and for each crop (in output
// pixels) <stem>_<name>.png: the panels side by side (bilinear, then each sharpness), each
// zoomed 2x with nearest neighbour so single pixels are visible.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "gpu/upscale_offline.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

struct Img {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;  // R, G, B, A
};

Img Bilinear(const Img& in, uint32_t ow, uint32_t oh) {
    Img o{ow, oh, std::vector<uint8_t>(size_t(ow) * oh * 4)};
    for (uint32_t y = 0; y < oh; ++y) {
        const double v = (y + 0.5) * double(in.h) / oh - 0.5;
        const int y0 = int(std::floor(v));
        const double fy = v - y0;
        const int ya = std::clamp(y0, 0, int(in.h) - 1), yb = std::clamp(y0 + 1, 0, int(in.h) - 1);
        for (uint32_t x = 0; x < ow; ++x) {
            const double u = (x + 0.5) * double(in.w) / ow - 0.5;
            const int x0 = int(std::floor(u));
            const double fx = u - x0;
            const int xa = std::clamp(x0, 0, int(in.w) - 1), xb = std::clamp(x0 + 1, 0, int(in.w) - 1);
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int xx, int yy) { return double(in.px[(size_t(yy) * in.w + xx) * 4 + c]); };
                const double r = (at(xa, ya) * (1 - fx) + at(xb, ya) * fx) * (1 - fy) + (at(xa, yb) * (1 - fx) + at(xb, yb) * fx) * fy;
                o.px[(size_t(y) * ow + x) * 4 + c] = uint8_t(std::clamp(std::lround(r), 0L, 255L));
            }
        }
    }
    return o;
}

bool WritePng(const std::string& path, const Img& img, int x0, int y0, int w, int h, int zoom) {
    std::vector<uint8_t> rgb(size_t(w) * zoom * h * zoom * 3);
    for (int y = 0; y < h * zoom; ++y)
        for (int x = 0; x < w * zoom; ++x) {
            const int sx = std::clamp(x0 + x / zoom, 0, int(img.w) - 1), sy = std::clamp(y0 + y / zoom, 0, int(img.h) - 1);
            const uint8_t* s = &img.px[(size_t(sy) * img.w + sx) * 4];
            uint8_t* d = &rgb[(size_t(y) * w * zoom + x) * 3];
            d[0] = s[0], d[1] = s[1], d[2] = s[2];
        }
    return stbi_write_png(path.c_str(), w * zoom, h * zoom, 3, rgb.data(), w * zoom * 3) != 0;
}

struct Crop {
    std::string name;
    int x, y, w, h;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: upeval <frame.raw> <out dir> [--size WxH] [--sharp a,b] [--crop name:x,y,w,h ...]\n");
        return 2;
    }
    const std::filesystem::path rawPath = argv[1], outDir = argv[2];
    uint32_t ow = 2560, oh = 1440;
    std::vector<float> sharps{0.25f};
    std::vector<Crop> crops;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--size" && i + 1 < argc) {
            std::sscanf(argv[++i], "%ux%u", &ow, &oh);
        } else if (a == "--sharp" && i + 1 < argc) {
            sharps.clear();
            std::string s = argv[++i];
            for (size_t p = 0; p <= s.size();) {
                const size_t q = s.find(',', p);
                sharps.push_back(float(std::atof(s.substr(p, q - p).c_str())));
                if (q == std::string::npos) break;
                p = q + 1;
            }
        } else if (a == "--crop" && i + 1 < argc) {
            const std::string s = argv[++i];
            const size_t colon = s.find(':');
            Crop c{s.substr(0, colon), 0, 0, 0, 0};
            std::sscanf(s.c_str() + colon + 1, "%d,%d,%d,%d", &c.x, &c.y, &c.w, &c.h);
            crops.push_back(c);
        }
    }
    Img in{1920, 1080, {}};
    {
        FILE* f = std::fopen(rawPath.string().c_str(), "rb");
        if (!f) {
            std::fprintf(stderr, "can't open %s\n", rawPath.string().c_str());
            return 1;
        }
        in.px.resize(size_t(in.w) * in.h * 4);
        const size_t got = std::fread(in.px.data(), 1, in.px.size(), f);
        std::fclose(f);
        if (got != in.px.size()) {
            std::fprintf(stderr, "%s: %zu bytes, expected %zu (1920x1080x4)\n", rawPath.string().c_str(), got, in.px.size());
            return 1;
        }
    }
    std::filesystem::create_directories(outDir);
    const std::string stem = rawPath.parent_path().filename().string() + "_" + rawPath.stem().string();

    std::vector<std::pair<std::string, Img>> variants;
    variants.emplace_back("bilinear", Bilinear(in, ow, oh));
    gpu::upgpu::OfflineUpscaler up;
    if (!up.Ok()) {
        std::fprintf(stderr, "no Vulkan device\n");
        return 1;
    }
    for (float s : sharps) {
        Img o{ow, oh, {}};
        double ms = -1, best = 1e9;
        for (int k = 0; k < 6; ++k) {
            if (!up.Run(in.px.data(), in.w, in.h, ow, oh, s, o.px, ms)) {
                std::fprintf(stderr, "upscale failed\n");
                return 1;
            }
            if (k >= 1 && ms >= 0) best = std::min(best, ms);
        }
        char name[32];
        std::snprintf(name, sizeof name, "fsr%.2f", s);
        std::printf("%s %s: gpu %.3f ms (best of 5, passes + readback copy)\n", stem.c_str(), name, best);
        variants.emplace_back(name, std::move(o));
    }
    for (const auto& [name, img] : variants)
        WritePng((outDir / (stem + "_" + name + ".png")).string(), img, 0, 0, int(img.w), int(img.h), 1);
    for (const Crop& c : crops) {
        // Panels side by side, 2x zoom, 8 px gap.
        const int zoom = 2, gap = 8, pw = c.w * zoom, ph = c.h * zoom;
        const int n = int(variants.size());
        std::vector<uint8_t> rgb(size_t(pw * n + gap * (n - 1)) * ph * 3, 40);
        const int W = pw * n + gap * (n - 1);
        for (int v = 0; v < n; ++v) {
            const Img& img = variants[v].second;
            for (int y = 0; y < ph; ++y)
                for (int x = 0; x < pw; ++x) {
                    const int sx = std::clamp(c.x + x / zoom, 0, int(img.w) - 1), sy = std::clamp(c.y + y / zoom, 0, int(img.h) - 1);
                    const uint8_t* s = &img.px[(size_t(sy) * img.w + sx) * 4];
                    uint8_t* d = &rgb[(size_t(y) * W + v * (pw + gap) + x) * 3];
                    d[0] = s[0], d[1] = s[1], d[2] = s[2];
                }
        }
        const std::string path = (outDir / (stem + "_" + c.name + ".png")).string();
        stbi_write_png(path.c_str(), W, ph, 3, rgb.data(), W * 3);
        std::printf("%s: panels", path.c_str());
        for (const auto& v : variants) std::printf(" %s", v.first.c_str());
        std::printf("\n");
    }
    return 0;
}
