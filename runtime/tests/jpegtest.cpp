// Offline check of the HLE libSceJpegDec / libSceJpegEnc. Calls them the way
// LBP3 does (eboot.bin+0x90f100 decodes a file, +0x412450 encodes a frame):
// null third arguments, sizes taken from the return values and from
// ParseHeader, ParseHeader's output pre-filled with garbage like the game's
// stack. Test JPEGs come from stb_image_write; real ones can be passed on the
// command line (e.g. a picture sticker pulled out of the game's .farc files,
// which must not be copied into the repo):
//
//   jpegtest [file.jpg ...]
//
// Prints the numbers it measures and exits 0 only if every check passes.
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "hle/jpeg.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
#pragma clang diagnostic pop

using namespace hle::jpeg;
using Bytes = std::vector<uint8_t>;

namespace {

int g_checks = 0, g_failures = 0;

__attribute__((format(printf, 2, 3))) bool Check(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        va_list args;
        va_start(args, fmt);
        std::printf("  FAIL: ");
        std::vprintf(fmt, args);
        std::printf("\n");
        va_end(args);
    }
    return ok;
}

constexpr uint8_t kGuard = 0xA5;
constexpr size_t kGuardSize = 256;

// A buffer of `size` bytes followed by guard bytes; 128-aligned like the game's.
struct Guarded {
    Bytes storage;
    uint8_t* data = nullptr;
    size_t size = 0;
    Guarded(size_t n, uint8_t fill) : storage(n + kGuardSize + 128, kGuard), size(n) {
        const auto base = reinterpret_cast<uintptr_t>(storage.data());
        data = storage.data() + ((128 - (base & 127)) & 127);
        std::memset(data, fill, n);
    }
    bool GuardIntact() const {
        for (size_t i = 0; i < kGuardSize; ++i) {
            if (data[size + i] != kGuard) return false;
        }
        return true;
    }
};

// ---- the game's decode sequence (eboot.bin+0x90f100) -------------------------

struct Decoded {
    int32_t parse = 0, query = 0, create = 0, decode = 0, del = 0;
    uint32_t width = 0, height = 0, extraBufferSize = 0;
    Bytes pixels;  // imageSize bytes as the decoder left them
    bool guardOk = true;
};

struct DecodeOptions {
    uint16_t pixelFormat = kDecR8G8B8A8;  // the game: 0
    uint16_t alpha = 0xFF;                // the game: 0xff
    uint32_t pitch = 0;                   // the game: 0
    int64_t imageSizeDelta = 0;           // the game: width * height * 4 exactly
};

Decoded GameDecode(const Bytes& file, const DecodeOptions& o = {}) {
    Decoded r;
    Guarded jpeg(file.size(), 0);
    if (!file.empty()) std::memcpy(jpeg.data, file.data(), file.size());

    DecParseParam parse{jpeg.data, static_cast<uint32_t>(file.size()), 0, 1};
    DecImageInfo info;
    std::memset(&info, 0xCD, sizeof(info));  // uninitialised stack in the game
    r.parse = sceJpegDecParseHeader(&parse, &info);
    r.extraBufferSize = info.extraBufferSize;  // the game allocates this before checking parse
    if (r.parse != 0) return r;
    r.width = info.imageWidth;
    r.height = info.imageHeight;

    DecCreateParam create{0xc, info.createAttr, info.outputWidth};
    r.query = sceJpegDecQueryMemorySize(&create);
    if (r.query == 0) return r;  // the game's only check
    Guarded memory((static_cast<uint32_t>(r.query) + 127) & ~127u, 0xEE);
    DecHandle handle = nullptr;
    r.create = sceJpegDecCreate(&create, memory.data, static_cast<uint32_t>(r.query), &handle);
    if (r.create != 0) return r;

    const int64_t imageSize = int64_t{r.width} * r.height * 4 + o.imageSizeDelta;
    Guarded image(static_cast<size_t>(imageSize), 0x77);
    DecDecodeParam p{};
    p.jpegData = jpeg.data;
    p.image = image.data;
    p.extraBuffer = nullptr;
    p.jpegSize = static_cast<uint32_t>(file.size());
    p.imageSize = static_cast<uint32_t>(imageSize);
    p.extraBufferSize = 0;
    p.option0 = parse.option0;
    p.option1 = parse.option1;
    p.pixelFormat = o.pixelFormat;
    p.alpha = o.alpha;
    p.pitch = o.pitch;
    r.decode = sceJpegDecDecode(handle, &p, nullptr);
    r.del = sceJpegDecDelete(handle);
    r.pixels.assign(image.data, image.data + imageSize);
    r.guardOk = image.GuardIntact() && memory.GuardIntact() && jpeg.GuardIntact();
    return r;
}

// ---- the game's encode sequence (eboot.bin+0x412450) -------------------------

struct Encoded {
    int32_t query = 0, create = 0, encode = 0, del = 0;
    Bytes jpeg;  // the first `encode` bytes when it succeeded
    bool guardOk = true;
};

struct EncodeOptions {
    uint16_t pixelFormat = kEncB8G8R8A8;  // the game: 1
    uint16_t colorSpace = kEncYcc;        // the game: 1
    uint8_t sampling = kEncSampling420;   // the game: 2
    uint32_t jpegCapacity = 0;            // the game: width * height
    uint32_t imageSize = 0;               // the game: width * height * 4
};

Encoded GameEncode(const uint8_t* image, uint32_t width, uint32_t height, uint32_t pitch, uint8_t quality,
                   const EncodeOptions& o = {}) {
    Encoded r;
    const uint32_t capacity = o.jpegCapacity ? o.jpegCapacity : width * height;
    Guarded jpeg(capacity, 0x33);
    EncCreateParam create{8, 0};
    r.query = sceJpegEncQueryMemorySize(&create);
    if (r.query <= 0) return r;
    Guarded memory(static_cast<uint32_t>(r.query), 0xEE);
    EncHandle handle = nullptr;
    r.create = sceJpegEncCreate(&create, memory.data, static_cast<uint32_t>(r.query), &handle);
    if (r.create != 0) return r;
    EncEncodeParam p{};
    p.image = image;
    p.jpeg = jpeg.data;
    p.imageSize = o.imageSize ? o.imageSize : width * height * 4;
    p.jpegSize = capacity;
    p.width = width;
    p.height = height;
    p.pitch = pitch;
    p.pixelFormat = o.pixelFormat;
    p.mode = kEncModeNormal;
    p.colorSpace = o.colorSpace;
    p.sampling = o.sampling;
    p.quality = quality;
    p.restartInterval = 0;
    r.encode = sceJpegEncEncode(handle, &p, nullptr);
    r.del = sceJpegEncDelete(handle);
    if (r.encode > 0 && static_cast<uint32_t>(r.encode) <= capacity) r.jpeg.assign(jpeg.data, jpeg.data + r.encode);
    r.guardOk = jpeg.GuardIntact() && memory.GuardIntact();
    return r;
}

// ---- images -------------------------------------------------------------------

struct Patch {
    uint32_t x, y, size;
    uint8_t r, g, b;
};

// Smooth gradients with flat patches of known colour (red, green, blue, white,
// black, yellow) so a channel swap or a misplaced row cannot pass.
Bytes MakeRgb(uint32_t w, uint32_t h, std::vector<Patch>* patches) {
    Bytes rgb(size_t{w} * h * 3);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = &rgb[(size_t{y} * w + x) * 3];
            p[0] = static_cast<uint8_t>(x * 255 / (w > 1 ? w - 1 : 1));
            p[1] = static_cast<uint8_t>(y * 255 / (h > 1 ? h - 1 : 1));
            p[2] = static_cast<uint8_t>(128 + 100 * std::sin((x + 2.0 * y) * 0.02));
        }
    }
    const uint8_t colours[6][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {0, 0, 0}, {255, 255, 0}};
    const uint32_t size = std::min(w, h) / 5;
    if (patches && size >= 24) {
        for (uint32_t i = 0; i < 6; ++i) {
            Patch pt{(i % 3) * (w / 3) + 8, (i / 3) * (h / 2) + 8, size, colours[i][0], colours[i][1], colours[i][2]};
            for (uint32_t y = pt.y; y < pt.y + size; ++y) {
                for (uint32_t x = pt.x; x < pt.x + size; ++x) {
                    uint8_t* p = &rgb[(size_t{y} * w + x) * 3];
                    p[0] = pt.r, p[1] = pt.g, p[2] = pt.b;
                }
            }
            patches->push_back(pt);
        }
    }
    return rgb;
}

// RGB -> 4 bytes per pixel with the given pitch, as B8G8R8A8 or R8G8B8A8.
Bytes To4(const Bytes& rgb, uint32_t w, uint32_t h, uint32_t pitch, bool bgra) {
    Bytes out(size_t{pitch} * h, 0x5A);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* s = &rgb[(size_t{y} * w + x) * 3];
            uint8_t* d = &out[size_t{y} * pitch + x * 4];
            d[0] = bgra ? s[2] : s[0], d[1] = s[1], d[2] = bgra ? s[0] : s[2], d[3] = 0xFF;
        }
    }
    return out;
}

// 4 bytes per pixel (with pitch) -> RGB.
Bytes From4(const Bytes& px, uint32_t w, uint32_t h, uint32_t pitch, bool bgra) {
    Bytes rgb(size_t{w} * h * 3);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* s = &px[size_t{y} * pitch + x * 4];
            uint8_t* d = &rgb[(size_t{y} * w + x) * 3];
            d[0] = bgra ? s[2] : s[0], d[1] = s[1], d[2] = bgra ? s[0] : s[2];
        }
    }
    return rgb;
}

struct Diff {
    double mean = 0, psnr = 0;
    int max = 0;
};

Diff Compare(const Bytes& a, const Bytes& b) {
    Diff d;
    if (a.size() != b.size() || a.empty()) return {1e9, 0, 255};
    double sum = 0, sq = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const int e = std::abs(int{a[i]} - int{b[i]});
        sum += e, sq += double(e) * e;
        d.max = std::max(d.max, e);
    }
    d.mean = sum / double(a.size());
    const double mse = sq / double(a.size());
    d.psnr = mse == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
    return d;
}

// Largest channel error over the middle of each flat patch.
int PatchError(const Bytes& rgb, uint32_t w, const std::vector<Patch>& patches) {
    int worst = 0;
    for (const Patch& p : patches) {
        for (uint32_t y = p.y + 8; y < p.y + p.size - 8; ++y) {
            for (uint32_t x = p.x + 8; x < p.x + p.size - 8; ++x) {
                const uint8_t* s = &rgb[(size_t{y} * w + x) * 3];
                worst = std::max({worst, std::abs(s[0] - p.r), std::abs(s[1] - p.g), std::abs(s[2] - p.b)});
            }
        }
    }
    return worst;
}

void StbSink(void* ctx, void* data, int size) {
    auto* out = static_cast<Bytes*>(ctx);
    out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}

Bytes StbEncode(const Bytes& rgb, uint32_t w, uint32_t h, int quality) {
    Bytes out;
    stbi_write_jpg_to_func(StbSink, &out, int(w), int(h), 3, rgb.data(), quality);
    return out;
}

// Reference decode, independent of the HLE code path.
Bytes StbDecode(const Bytes& jpeg, int* w, int* h) {
    int comp = 0;
    stbi_uc* p = stbi_load_from_memory(jpeg.data(), int(jpeg.size()), w, h, &comp, 3);
    if (!p) return {};
    Bytes out(p, p + size_t(*w) * size_t(*h) * 3);
    stbi_image_free(p);
    return out;
}

bool IsJfif(const Bytes& j) {
    return j.size() > 4 && j[0] == 0xFF && j[1] == 0xD8 && j[2] == 0xFF && j[3] == 0xE0 && j[j.size() - 2] == 0xFF &&
           j.back() == 0xD9;
}

// ---- tests ----------------------------------------------------------------------

void TestDecodeSynthetic() {
    std::printf("decode: stb-written JPEGs, as the game decodes a file\n");
    const uint32_t sizes[][2] = {{320, 200}, {1, 1}, {17, 9}, {641, 3}, {1920, 1080}};
    for (const auto& s : sizes) {
        const uint32_t w = s[0], h = s[1];
        std::vector<Patch> patches;
        const Bytes rgb = MakeRgb(w, h, &patches);
        const Bytes file = StbEncode(rgb, w, h, 95);
        const Decoded d = GameDecode(file);
        const int32_t expect = static_cast<int32_t>((w << 16) | h);
        Check(d.parse == 0 && d.width == w && d.height == h, "%ux%u parse %08X -> %ux%u", w, h, uint32_t(d.parse),
              d.width, d.height);
        Check(d.extraBufferSize == 0, "%ux%u extra buffer %u", w, h, d.extraBufferSize);
        Check(d.query > 0 && d.create == 0 && d.del == 0, "%ux%u query %d create %08X delete %08X", w, h, d.query,
              uint32_t(d.create), uint32_t(d.del));
        Check(d.decode == expect, "%ux%u decode returned %08X, want %08X", w, h, uint32_t(d.decode),
              uint32_t(expect));
        Check(d.guardOk, "%ux%u wrote past a buffer", w, h);
        if (d.decode <= 0) continue;
        int rw = 0, rh = 0;
        const Bytes ref = StbDecode(file, &rw, &rh);
        const Bytes got = From4(d.pixels, w, h, w * 4, false);
        const Diff vsRef = Compare(got, ref), vsSrc = Compare(got, rgb);
        bool alphaOk = true;
        for (size_t i = 3; i < d.pixels.size(); i += 4) alphaOk &= d.pixels[i] == 0xFF;
        Check(vsRef.max == 0, "%ux%u differs from stb's own decode (max %d)", w, h, vsRef.max);
        Check(alphaOk, "%ux%u alpha not 0xFF", w, h);
        const int patchErr = patches.empty() ? 0 : PatchError(got, w, patches);
        Check(patchErr <= 8, "%ux%u flat patches off by %d", w, h, patchErr);
        std::printf("  %4ux%-4u %6zu bytes q95: vs source mean %.2f max %d PSNR %.1f dB, patches max %d, "
                    "vs stb exact\n",
                    w, h, file.size(), vsSrc.mean, vsSrc.max, vsSrc.psnr, patchErr);
    }
}

void TestDecodeFormats() {
    std::printf("decode: pixel format, alpha, pitch, small buffers\n");
    const uint32_t w = 320, h = 200;
    std::vector<Patch> patches;
    const Bytes rgb = MakeRgb(w, h, &patches);
    const Bytes file = StbEncode(rgb, w, h, 95);
    const Decoded rgba = GameDecode(file);

    DecodeOptions bgraOpt;
    bgraOpt.pixelFormat = kDecB8G8R8A8;
    bgraOpt.alpha = 0x80;
    const Decoded bgra = GameDecode(file, bgraOpt);
    Check(bgra.decode > 0 && bgra.guardOk, "BGRA decode %08X", uint32_t(bgra.decode));
    if (bgra.decode > 0) {
        Check(Compare(From4(bgra.pixels, w, h, w * 4, true), From4(rgba.pixels, w, h, w * 4, false)).max == 0,
              "BGRA is not RGBA with R and B swapped");
        // The red patch: R byte first in RGBA, third in BGRA.
        const Patch& p = patches[0];
        const size_t at = (size_t{p.y + p.size / 2} * w + p.x + p.size / 2) * 4;
        Check(rgba.pixels[at] > 240 && rgba.pixels[at + 2] < 16, "RGBA red patch reads %u,%u,%u", rgba.pixels[at],
              rgba.pixels[at + 1], rgba.pixels[at + 2]);
        Check(bgra.pixels[at] < 16 && bgra.pixels[at + 2] > 240, "BGRA red patch reads %u,%u,%u",
              bgra.pixels[at], bgra.pixels[at + 1], bgra.pixels[at + 2]);
        bool alphaOk = true;
        for (size_t i = 3; i < bgra.pixels.size(); i += 4) alphaOk &= bgra.pixels[i] == 0x80;
        Check(alphaOk, "alpha 0x80 not applied");
    }

    DecodeOptions pitchOpt;
    pitchOpt.pitch = w * 4 + 20;
    pitchOpt.imageSizeDelta = int64_t{20} * h;
    const Decoded pitched = GameDecode(file, pitchOpt);
    Check(pitched.decode > 0 && pitched.guardOk, "pitch %u decode %08X", pitchOpt.pitch, uint32_t(pitched.decode));
    if (pitched.decode > 0) {
        bool padOk = true;
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t i = 0; i < 20; ++i) padOk &= pitched.pixels[size_t{y} * pitchOpt.pitch + w * 4 + i] == 0x77;
        }
        Check(padOk, "pitch padding was written");
        Check(Compare(From4(pitched.pixels, w, h, pitchOpt.pitch, false), From4(rgba.pixels, w, h, w * 4, false)).max ==
                  0,
              "pitched rows differ");
    }

    DecodeOptions small;
    small.imageSizeDelta = -1;
    const Decoded tooSmall = GameDecode(file, small);
    bool untouched = true;
    for (uint8_t b : tooSmall.pixels) untouched &= b == 0x77;
    Check(tooSmall.decode == kDecErrorInvalidSize && untouched && tooSmall.guardOk,
          "1-byte-short buffer: decode %08X, untouched %d", uint32_t(tooSmall.decode), untouched);

    DecodeOptions narrow;
    narrow.pitch = w * 4 - 4;
    Check(GameDecode(file, narrow).decode == kDecErrorInvalidParam, "pitch below width*4 accepted");
    DecodeOptions badFormat;
    badFormat.pixelFormat = 7;
    Check(GameDecode(file, badFormat).decode == kDecErrorInvalidParam, "pixel format 7 accepted");
    std::printf("  RGBA/BGRA swap, alpha 0x80, pitch +20, 1-byte-short buffer, narrow pitch, bad format: checked\n");
}

Bytes Seg(std::initializer_list<int> bytes) {
    Bytes out;
    for (int b : bytes) out.push_back(static_cast<uint8_t>(b));
    return out;
}

void TestDecodeMalformed() {
    std::printf("decode: malformed input\n");
    const uint8_t soi[] = {0xFF, 0xD8};
    auto withSof = [&](int marker, int precision, int height, int width, int comps, int len) {
        Bytes b = Seg({0xFF, 0xD8, 0xFF, marker, len >> 8, len & 0xFF, precision, height >> 8, height & 0xFF,
                       width >> 8, width & 0xFF, comps});
        for (int i = 0; i < comps; ++i) b.insert(b.end(), {uint8_t(i + 1), 0x11, 0});
        b.insert(b.end(), {0xFF, 0xD9});
        return b;
    };
    std::mt19937 rng(1234);
    Bytes random(4096);
    for (auto& b : random) b = static_cast<uint8_t>(rng());
    Bytes randomSoi = random;
    randomSoi[0] = 0xFF, randomSoi[1] = 0xD8;

    struct Case {
        const char* name;
        Bytes data;
    } cases[] = {
        {"empty", {}},
        {"PNG signature", Seg({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13})},
        {"SOI only", Bytes(soi, soi + 2)},
        {"SOI EOI", Seg({0xFF, 0xD8, 0xFF, 0xD9})},
        {"scan before frame", Seg({0xFF, 0xD8, 0xFF, 0xDA, 0, 8, 1, 1, 0, 0, 0x3F, 0})},
        {"segment longer than file", Seg({0xFF, 0xD8, 0xFF, 0xE0, 0x40, 0x00, 'J', 'F'})},
        {"segment length 1", Seg({0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x01, 0xFF, 0xD9})},
        {"12-bit samples", withSof(0xC0, 12, 8, 8, 3, 17)},
        {"lossless SOF3", withSof(0xC3, 8, 8, 8, 3, 17)},
        {"arithmetic SOF9", withSof(0xC9, 8, 8, 8, 3, 17)},
        {"width 0", withSof(0xC0, 8, 8, 0, 3, 17)},
        {"height 0 (DNL)", withSof(0xC0, 8, 0, 8, 3, 17)},
        {"2 components", withSof(0xC0, 8, 8, 8, 2, 14)},
        {"frame header too short", withSof(0xC0, 8, 8, 8, 3, 9)},
        {"width 40000", withSof(0xC0, 8, 8, 40000, 3, 17)},
        {"random bytes", random},
        {"random bytes after SOI", randomSoi},
        {"frame, no scan", withSof(0xC0, 8, 8, 8, 3, 17)},
    };
    for (const Case& c : cases) {
        const Decoded d = GameDecode(c.data);
        Check(d.parse < 0 && d.extraBufferSize == 0 && d.guardOk, "%s: parse %08X decode %08X extra %u", c.name,
              uint32_t(d.parse), uint32_t(d.decode), d.extraBufferSize);
        std::printf("  %-26s parse %08X\n", c.name, uint32_t(d.parse));
    }
    // A complete header with no tables and no entropy data: the header parses,
    // and the decode may refuse it or produce some image, but must stay in bounds.
    Bytes noData = withSof(0xC0, 8, 16, 16, 3, 17);
    noData.resize(noData.size() - 2);
    noData.insert(noData.end(), {0xFF, 0xDA, 0, 12, 3, 1, 0, 2, 0x11, 3, 0x11, 0, 0x3F, 0, 0xFF, 0xD9});
    const Decoded nd = GameDecode(noData);
    Check(nd.parse == 0 && nd.guardOk && (nd.decode < 0 || nd.decode == (16 << 16 | 16)),
          "scan with no tables or data: parse %08X decode %08X", uint32_t(nd.parse), uint32_t(nd.decode));
    std::printf("  %-26s parse %08X decode %08X\n", "scan with no tables/data", uint32_t(nd.parse),
                uint32_t(nd.decode));
    // A null info pointer and a null param must be refused, not dereferenced.
    Check(sceJpegDecParseHeader(nullptr, nullptr) == kDecErrorInvalidAddr, "ParseHeader(null, null)");
}

void TestDecodeTruncatedAndCorrupt() {
    std::printf("decode: truncated and corrupted files\n");
    const uint32_t w = 320, h = 200;
    const Bytes file = StbEncode(MakeRgb(w, h, nullptr), w, h, 95);
    // Where the header ends (after the scan header): a shorter prefix must fail to parse.
    size_t headerEnd = 0;
    for (size_t i = 2; i + 4 < file.size(); ++i) {
        if (file[i] == 0xFF && file[i + 1] == 0xDA) {
            headerEnd = i + 2 + (size_t{file[i + 2]} << 8 | file[i + 3]);
            break;
        }
    }
    int parsedEarly = 0, decodedOk = 0, decodedErr = 0, guardBroken = 0, attempts = 0;
    for (size_t len = 0; len < file.size(); len += (len < headerEnd + 64 ? 1 : 61)) {
        const Bytes cut(file.begin(), file.begin() + static_cast<ptrdiff_t>(len));
        // Size the output from the full image so the decoder could overrun if it tried.
        const Decoded d = GameDecode(cut);
        ++attempts;
        if (len < headerEnd && d.parse == 0) ++parsedEarly;
        if (d.parse == 0) (d.decode > 0 ? decodedOk : decodedErr)++;
        if (!d.guardOk) ++guardBroken;
    }
    Check(parsedEarly == 0, "%d prefixes shorter than the header parsed", parsedEarly);
    Check(guardBroken == 0, "%d truncated decodes wrote past a buffer", guardBroken);
    std::printf("  %d prefixes of a %zu-byte file (header ends at %zu): all shorter ones refused; "
                "past it %d decoded (stb fills the missing rows), %d refused; no overruns\n",
                attempts, file.size(), headerEnd, decodedOk, decodedErr);

    std::mt19937 rng(99);
    int corruptOk = 0, corruptErr = 0;
    guardBroken = 0;
    for (int i = 0; i < 300; ++i) {
        Bytes bad = file;
        for (int k = 0; k < 1 + i % 8; ++k) bad[rng() % bad.size()] = static_cast<uint8_t>(rng());
        const Decoded d = GameDecode(bad);
        if (d.parse == 0) (d.decode > 0 ? corruptOk : corruptErr)++;
        if (!d.guardOk || (d.decode > 0 && d.decode != static_cast<int32_t>((d.width << 16) | d.height))) ++guardBroken;
    }
    Check(guardBroken == 0, "%d corrupted decodes overran or returned a wrong size", guardBroken);
    std::printf("  300 files with 1-8 random bytes changed: %d decoded, %d refused, %d header refused; no overruns\n",
                corruptOk, corruptErr, 300 - corruptOk - corruptErr);
}

void TestEncodeLikeTheGame() {
    std::printf("encode: the compression ratio maps to the firmware's quantisers (m2/firmware.md)\n");
    {
        const struct { uint32_t ratio; int quality; } map[] = {{0, 100}, {1, 100}, {50, 80}, {70, 73},
                                                               {90, 65}, {128, 50}, {255, 17}};
        for (const auto& m : map)
            Check(hle::jpeg::QualityFromRatio(m.ratio) == m.quality, "ratio %u gives quality %d, not %d", m.ratio,
                  hle::jpeg::QualityFromRatio(m.ratio), m.quality);
    }
    std::printf("encode: B8G8R8A8 frames as the game sends them (pitch 4w, buffer w*h, ratio 70/90/50)\n");
    const struct {
        uint32_t w, h;
        uint8_t q;
    } shots[] = {{960, 540, 70}, {480, 270, 90}, {640, 360, 50}, {1920, 1080, 70}, {33, 31, 70}};
    for (const auto& s : shots) {
        std::vector<Patch> patches;
        const Bytes rgb = MakeRgb(s.w, s.h, &patches);
        const Bytes bgra = To4(rgb, s.w, s.h, s.w * 4, true);
        const Encoded e = GameEncode(bgra.data(), s.w, s.h, s.w * 4, s.q);
        Check(e.query > 0 && e.create == 0 && e.del == 0, "%ux%u query %d create %08X", s.w, s.h, e.query,
              uint32_t(e.create));
        Check(e.encode > 0 && static_cast<uint32_t>(e.encode) <= s.w * s.h, "%ux%u q%u encode returned %08X", s.w,
              s.h, s.q, uint32_t(e.encode));
        Check(e.guardOk, "%ux%u encode wrote past a buffer", s.w, s.h);
        if (e.encode <= 0) continue;
        Check(IsJfif(e.jpeg), "%ux%u output is not FF D8 FF E0 ... FF D9 (the game's loader needs JFIF)", s.w, s.h);
        // Same bytes as stb fed the source RGB directly: the BGRA unpacking is exact.
        Check(e.jpeg == StbEncode(rgb, s.w, s.h, hle::jpeg::QualityFromRatio(s.q)), "%ux%u output differs from stb on the RGB source", s.w, s.h);
        int rw = 0, rh = 0;
        const Bytes ref = StbDecode(e.jpeg, &rw, &rh);
        Check(!ref.empty() && uint32_t(rw) == s.w && uint32_t(rh) == s.h, "%ux%u reference decode %dx%d", s.w, s.h,
              rw, rh);
        if (ref.empty()) continue;
        const Diff diff = Compare(ref, rgb);
        const int patchErr = patches.empty() ? 0 : PatchError(ref, s.w, patches);
        // Sanity bounds only (JPEG loss at q50-q90 with 4:2:0 chroma); a swapped
        // channel would put the red and blue patches about 255 off.
        Check(diff.psnr > 28.0 && patchErr <= 40, "%ux%u q%u PSNR %.1f dB, patches off by %d", s.w, s.h, s.q,
              diff.psnr, patchErr);
        // And back through the HLE decoder, as RGBA (what the game's texture path wants).
        const Decoded back = GameDecode(e.jpeg);
        Check(back.decode > 0 && Compare(From4(back.pixels, s.w, s.h, s.w * 4, false), ref).max == 0,
              "%ux%u HLE decode of the HLE encode differs from stb", s.w, s.h);
        std::printf("  %4ux%-4u q%u -> %7d bytes (%.2f of the w*h buffer), == stb, mean err %.2f, "
                    "PSNR %.1f dB, patches max %d\n",
                    s.w, s.h, s.q, e.encode, double(e.encode) / (s.w * s.h), diff.mean, diff.psnr, patchErr);
    }
    // A JPEG's tables alone take about 600 bytes, so a tiny frame cannot fit in
    // the game's w*h buffer: refused cleanly. (The game's smallest is a quarter screen.)
    const uint32_t tw = 17, th = 9;
    const Bytes tiny = To4(MakeRgb(tw, th, nullptr), tw, th, tw * 4, true);
    const Encoded refused = GameEncode(tiny.data(), tw, th, tw * 4, 70);
    EncodeOptions roomy;
    roomy.jpegCapacity = 4096;
    const Encoded fits = GameEncode(tiny.data(), tw, th, tw * 4, 70, roomy);
    Check(refused.encode == kEncErrorInvalidSize && refused.guardOk, "17x9 in 153 bytes: %08X",
          uint32_t(refused.encode));
    Check(fits.encode > 0 && IsJfif(fits.jpeg) && fits.guardOk, "17x9 in 4096 bytes: %08X", uint32_t(fits.encode));
    std::printf("    17x9 q70: %08X with the game's 153-byte buffer, %d bytes with 4096\n", uint32_t(refused.encode),
                fits.encode);
}

void TestEncodeOtherFormats() {
    std::printf("encode: R8G8B8A8, Y8, Y8U8Y8V8, padded pitch\n");
    const uint32_t w = 256, h = 128;
    std::vector<Patch> patches;
    const Bytes rgb = MakeRgb(w, h, &patches);
    int rw = 0, rh = 0;

    const Bytes rgba = To4(rgb, w, h, w * 4 + 64, false);
    EncodeOptions o;
    o.pixelFormat = kEncR8G8B8A8;
    o.imageSize = (w * 4 + 64) * h;
    const Encoded e1 = GameEncode(rgba.data(), w, h, w * 4 + 64, 90, o);
    const Bytes d1 = e1.encode > 0 ? StbDecode(e1.jpeg, &rw, &rh) : Bytes{};
    const Diff diff1 = Compare(d1, rgb);
    Check(e1.encode > 0 && e1.jpeg == StbEncode(rgb, w, h, hle::jpeg::QualityFromRatio(90)) && PatchError(d1, w, patches) <= 40,
          "RGBA, pitch +64: %08X, not the same bytes as stb on the RGB source", uint32_t(e1.encode));
    std::printf("  RGBA pitch %u: %d bytes, == stb, PSNR %.1f dB\n", w * 4 + 64, e1.encode, diff1.psnr);

    Bytes grey(size_t{w} * h);
    for (size_t i = 0; i < grey.size(); ++i) grey[i] = static_cast<uint8_t>((i * 7) % 251);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) grey[size_t{y} * w + x] = static_cast<uint8_t>((x + y) * 255 / (w + h));
    }
    o = {};
    o.pixelFormat = kEncY8, o.colorSpace = kEncGrayscale, o.sampling = kEncSamplingFull, o.imageSize = w * h;
    const Encoded e2 = GameEncode(grey.data(), w, h, w, 90, o);
    Bytes greyRgb(grey.size() * 3);
    for (size_t i = 0; i < grey.size(); ++i) greyRgb[3 * i] = greyRgb[3 * i + 1] = greyRgb[3 * i + 2] = grey[i];
    const Bytes d2 = e2.encode > 0 ? StbDecode(e2.jpeg, &rw, &rh) : Bytes{};
    const Diff diff2 = Compare(d2, greyRgb);
    Check(e2.encode > 0 && diff2.psnr > 35, "Y8: %08X PSNR %.1f", uint32_t(e2.encode), diff2.psnr);
    std::printf("  Y8: %d bytes, PSNR %.1f dB\n", e2.encode, diff2.psnr);

    // Y8U8Y8V8 of flat colours: JFIF YCbCr of red, green, blue, grey in vertical bands.
    const uint8_t bands[4][3] = {{76, 85, 255}, {150, 44, 21}, {29, 255, 107}, {128, 128, 128}};
    const uint8_t bandRgb[4][3] = {{254, 0, 0}, {0, 255, 1}, {0, 0, 255}, {128, 128, 128}};
    Bytes yuyv(size_t{w} * 2 * h), expect(size_t{w} * h * 3);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; x += 2) {
            const uint32_t b = x * 4 / w;
            uint8_t* p = &yuyv[size_t{y} * w * 2 + x * 2];
            p[0] = bands[b][0], p[1] = bands[b][1], p[2] = bands[b][0], p[3] = bands[b][2];
            for (uint32_t k = 0; k < 2; ++k) std::memcpy(&expect[(size_t{y} * w + x + k) * 3], bandRgb[b], 3);
        }
    }
    o = {};
    o.pixelFormat = kEncY8U8Y8V8, o.imageSize = w * 2 * h;
    const Encoded e3 = GameEncode(yuyv.data(), w, h, w * 2, 90, o);
    const Bytes d3 = e3.encode > 0 ? StbDecode(e3.jpeg, &rw, &rh) : Bytes{};
    int bandErr = 0;
    if (!d3.empty()) {
        for (uint32_t b = 0; b < 4; ++b) {
            const size_t at = (size_t{h / 2} * w + b * w / 4 + w / 8) * 3;
            for (int c = 0; c < 3; ++c) bandErr = std::max(bandErr, std::abs(d3[at + c] - bandRgb[b][c]));
        }
    }
    Check(e3.encode > 0 && bandErr <= 10, "YUYV: %08X band error %d", uint32_t(e3.encode), bandErr);
    std::printf("  Y8U8Y8V8: %d bytes, band centres off by %d\n", e3.encode, bandErr);
}

void TestEncodeRefusals() {
    std::printf("encode: refusals and small buffers\n");
    const uint32_t w = 960, h = 540;
    const Bytes bgra = To4(MakeRgb(w, h, nullptr), w, h, w * 4, true);

    EncodeOptions tiny;
    tiny.jpegCapacity = 600;
    const Encoded small = GameEncode(bgra.data(), w, h, w * 4, 70, tiny);
    Check(small.encode == kEncErrorInvalidSize && small.guardOk, "600-byte buffer: %08X, guard %d",
          uint32_t(small.encode), small.guardOk);
    // Just big enough versus one byte short.
    const Encoded full = GameEncode(bgra.data(), w, h, w * 4, 70);
    if (full.encode > 0) {
        EncodeOptions exact;
        exact.jpegCapacity = static_cast<uint32_t>(full.encode);
        const Encoded fits = GameEncode(bgra.data(), w, h, w * 4, 70, exact);
        exact.jpegCapacity -= 1;
        const Encoded shortBy1 = GameEncode(bgra.data(), w, h, w * 4, 70, exact);
        Check(fits.encode == full.encode && fits.guardOk, "exact-size buffer: %08X", uint32_t(fits.encode));
        Check(shortBy1.encode == kEncErrorInvalidSize && shortBy1.guardOk, "1-byte-short buffer: %08X",
              uint32_t(shortBy1.encode));
    }

    EncodeOptions o;
    o.sampling = kEncSamplingFull;
    Check(GameEncode(bgra.data(), w, h, w * 4, 70, o).encode == kEncErrorInvalidParam, "BGRA with full sampling");
    o = {};
    o.colorSpace = kEncGrayscale;
    Check(GameEncode(bgra.data(), w, h, w * 4, 70, o).encode == kEncErrorInvalidParam, "BGRA as grayscale");
    Check(GameEncode(bgra.data(), w, h, w * 4 - 4, 70).encode == kEncErrorInvalidParam, "pitch below 4w");
    o = {};
    o.imageSize = w * h * 4 - 1;
    Check(GameEncode(bgra.data(), w, h, w * 4, 70, o).encode == kEncErrorInvalidParam, "image smaller than pitch*h");
    Check(GameEncode(bgra.data() + 1, w, h - 1, w * 4, 70).encode == kEncErrorInvalidAddr, "unaligned image");
    Check(GameEncode(nullptr, w, h, w * 4, 70).encode == kEncErrorInvalidAddr, "null image");
    o = {};
    o.pixelFormat = 5;
    Check(GameEncode(bgra.data(), w, h, w * 4, 70, o).encode == kEncErrorInvalidParam, "pixel format 5");
    o = {};
    o.jpegCapacity = 4096, o.imageSize = w * h * 4;
    Check(GameEncode(bgra.data(), 0, h, w * 4, 70, o).encode == kEncErrorInvalidParam, "width 0");
    std::printf("  600-byte buffer %08X; exact-size buffer ok, 1 byte short refused; bad params refused\n",
                uint32_t(small.encode));
}

void TestHandles() {
    std::printf("handles and memory\n");
    alignas(64) static uint8_t memory[0x900];
    DecCreateParam dc{0xc, 0, 64};
    EncCreateParam ec{8, 0};
    DecHandle dec = nullptr;
    EncHandle enc = nullptr;
    Check(sceJpegDecQueryMemorySize(&dc) > 0, "Dec QueryMemorySize");
    Check(sceJpegEncQueryMemorySize(&ec) > 0, "Enc QueryMemorySize");
    DecCreateParam badSize{8, 0, 64};
    EncCreateParam badAttr{8, 1};
    Check(sceJpegDecQueryMemorySize(&badSize) < 0, "Dec QueryMemorySize with size 8");
    Check(sceJpegEncQueryMemorySize(&badAttr) < 0, "Enc QueryMemorySize with attr 1");
    // The game passes (null, 0) when QueryMemorySize failed and its allocation did too.
    Check(sceJpegDecCreate(&dc, nullptr, 0, &dec) == kDecErrorInvalidAddr, "DecCreate(null, 0)");
    Check(sceJpegEncCreate(&ec, nullptr, 0, &enc) == kEncErrorInvalidAddr, "EncCreate(null, 0)");
    Check(sceJpegDecCreate(&dc, memory, 0x10, &dec) == kDecErrorInvalidSize, "DecCreate with 16 bytes");
    Check(sceJpegDecCreate(&dc, memory + 1, 0x800, &dec) == 0, "DecCreate at an odd address");
    const auto* at = static_cast<uint8_t*>(dec);
    Check((reinterpret_cast<uintptr_t>(at) & 31) == 0 && at >= memory + 1 && at < memory + 0x801,
          "handle inside the given memory, 32-byte aligned");

    const Bytes file = StbEncode(MakeRgb(64, 64, nullptr), 64, 64, 90);
    Bytes image(64 * 64 * 4);
    DecDecodeParam p{};
    p.jpegData = file.data(), p.image = image.data(), p.jpegSize = uint32_t(file.size());
    p.imageSize = uint32_t(image.size()), p.option1 = 1, p.alpha = 0xFF;
    Check(sceJpegDecDecode(dec, &p, nullptr) == (64 << 16 | 64), "decode through the odd-address handle");
    DecImageInfo info;
    std::memset(&info, 0xCD, sizeof(info));
    Check(sceJpegDecDecode(dec, &p, &info) > 0 && info.imageWidth == 64 && info.outputHeight == 64 &&
              info.extraBufferSize == 0,
          "Decode fills the image info when asked");
    Check(sceJpegEncEncode(dec, nullptr, nullptr) == kEncErrorInvalidHandle, "encode with a decoder handle");
    Check(sceJpegDecDecode(nullptr, &p, nullptr) == kDecErrorInvalidHandle, "decode with a null handle");
    alignas(32) uint8_t junk[64] = {};
    Check(sceJpegDecDecode(junk, &p, nullptr) == kDecErrorInvalidHandle, "decode with a junk handle");

    DecCreateParam narrow{0xc, 0, 32};
    DecHandle small = nullptr;
    alignas(32) static uint8_t memory2[0x800];
    Check(sceJpegDecCreate(&narrow, memory2, sizeof(memory2), &small) == 0, "DecCreate maxWidth 32");
    Check(sceJpegDecDecode(small, &p, nullptr) == kDecErrorInvalidParam, "64-wide image on a maxWidth-32 handle");
    Check(sceJpegDecDelete(small) == 0, "delete");

    Check(sceJpegDecDelete(dec) == 0, "delete");
    Check(sceJpegDecDelete(dec) == kDecErrorInvalidHandle, "delete twice");
    Check(sceJpegDecDecode(dec, &p, nullptr) == kDecErrorInvalidHandle, "decode after delete");
    std::printf("  create/query/delete rules checked\n");
}

void TestRealFiles(int argc, char** argv) {
    if (argc < 2) {
        std::printf("real files: none given\n");
        return;
    }
    std::printf("real files: decode as the game does, compare with stb, re-encode as BGRA\n");
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary);
        const Bytes file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (!Check(!file.empty(), "cannot read %s", argv[i])) continue;
        const Decoded d = GameDecode(file);
        int rw = 0, rh = 0;
        const Bytes ref = StbDecode(file, &rw, &rh);
        const bool ok = d.decode == static_cast<int32_t>((d.width << 16) | d.height) && d.guardOk &&
                        uint32_t(rw) == d.width && uint32_t(rh) == d.height &&
                        Compare(From4(d.pixels, d.width, d.height, d.width * 4, false), ref).max == 0;
        Check(ok, "%s: parse %08X decode %08X %ux%u (stb %dx%d)", argv[i], uint32_t(d.parse), uint32_t(d.decode),
              d.width, d.height, rw, rh);
        if (!ok) continue;
        const Bytes bgra = To4(ref, d.width, d.height, d.width * 4, true);
        const Encoded e = GameEncode(bgra.data(), d.width, d.height, d.width * 4, 70);
        const Bytes again = e.encode > 0 ? StbDecode(e.jpeg, &rw, &rh) : Bytes{};
        const Diff diff = Compare(again, ref);
        Check(e.encode > 0 && IsJfif(e.jpeg) && diff.psnr > 30, "%s: re-encode %08X PSNR %.1f", argv[i],
              uint32_t(e.encode), diff.psnr);
        if (n++ < 6) {
            std::printf("  %s: %zu bytes, %ux%u, decoded == stb; re-encoded q70 %d bytes, PSNR %.1f dB\n", argv[i],
                        file.size(), d.width, d.height, e.encode, diff.psnr);
        }
    }
    if (n > 6) std::printf("  ... %d files in all\n", n);
}

}  // namespace

int main(int argc, char** argv) {
    TestDecodeSynthetic();
    TestDecodeFormats();
    TestDecodeMalformed();
    TestDecodeTruncatedAndCorrupt();
    TestEncodeLikeTheGame();
    TestEncodeOtherFormats();
    TestEncodeRefusals();
    TestHandles();
    TestRealFiles(argc, argv);
    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
