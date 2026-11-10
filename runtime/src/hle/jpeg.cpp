// libSceJpegDec and libSceJpegEnc, decoding with stb_image and encoding with
// stb_image_write. Milestone 2, gap 13. Notes: .scratch/lbp3-port/m2/jpeg.md.
//
// Who calls them:
// - JpegEnc: photos of the scene taken from the Popit (capture mode 1 of
//   eboot.bin+0x702c80; Capture Object, mode 2, saves a texture instead). The
//   game reads back the frame as B8G8R8A8, encodes it at half the render size
//   with quality 70 and keeps the JPEG as the photo's image. A quality-90
//   thumbnail is also encoded when a state byte the cross-play code keeps
//   equals 5, which offline play presumably never sees. The game reads photos
//   back with the IJG libjpeg linked into the eboot, through a texture loader
//   that only takes data starting FF D8 FF E0 (JFIF) or FF D8 FF EE; stb
//   writes the JFIF header first. The PlayStation Camera snapshot (quality 50)
//   and the grief-report screenshot (online) use the same encoder.
// - JpegDec: its one caller loads image files by path, and the paths come from
//   libSceContentSearch results (the PS4 capture gallery). Content search stays
//   a stub that finds nothing, so offline play never gets here; it is
//   implemented so the path works if that changes.
//
// Memory: Create gets memory the game sized with QueryMemorySize. The handle
// header sits at its start (32-byte aligned and pointing at itself, as in
// shadPS4's JpegEnc), so Delete leaves nothing to free. stb's working memory
// comes from the host heap.
#include "hle/jpeg.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

#include "hle/kernel_errors.h"
#include "hle/register.h"

// stb in this file only: static functions keep it from clashing with any other
// copy in the link, and its own warnings stay out of the build.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
// Zeroed allocations: a truncated progressive file leaves coefficients that no
// scan wrote, and those must not carry old host heap contents into the image.
#define STBI_MALLOC(size) std::calloc(1, size)
#define STBI_REALLOC(p, size) std::realloc(p, size)
#define STBI_FREE(p) std::free(p)
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

namespace hle::jpeg {
namespace {

// What QueryMemorySize asks for. Only the handle header lives there; shadPS4's
// JpegEnc uses the same minimum.
constexpr uint32_t kMemorySize = 0x800;
constexpr uintptr_t kHandleAlign = 0x20;
constexpr uint32_t kDecMagic = 0x4A504544;  // "DEPJ"
constexpr uint32_t kEncMagic = 0x4A50454E;  // "NEPJ"

// Decode returns (width << 16) | height, which has to stay positive: the game
// treats a result <= 0 as failure.
constexpr uint32_t kDecMaxWidth = 0x7FFF;

constexpr uint32_t kEncMaxDimension = 0xFFFF;
constexpr uint32_t kEncMaxPitch = 0xFFFFFFF;
constexpr uint64_t kEncMaxImageSize = 0x7FFFFFFF;

struct Handle {
    Handle* self;  // == this while the handle is live
    uint32_t magic;
    uint32_t maxWidth;
};

// Logs the first few calls of each kind, then one in 256.
bool ShouldLog(std::atomic<uint32_t>& count) {
    const uint32_t n = count.fetch_add(1, std::memory_order_relaxed);
    return n < 16 || (n & 255) == 0;
}
std::atomic<uint32_t> g_decLogs{0}, g_encLogs{0}, g_errLogs{0};

__attribute__((format(printf, 2, 3))) int32_t Fail(int32_t code, const char* fmt, ...) {
    if (ShouldLog(g_errLogs)) {
        char text[256];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        std::fprintf(stderr, "[jpeg] %s -> %08X\n", text, static_cast<uint32_t>(code));
    }
    return code;
}

bool MakeHandle(void* memory, uint32_t memorySize, uint32_t magic, uint32_t maxWidth, void** out) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(memory);
    const uintptr_t aligned = (base + kHandleAlign - 1) & ~(kHandleAlign - 1);
    if (aligned - base + sizeof(Handle) > memorySize) return false;
    auto* h = reinterpret_cast<Handle*>(aligned);
    h->self = h;
    h->magic = magic;
    h->maxWidth = maxWidth;
    *out = h;
    return true;
}

Handle* LiveHandle(void* handle, uint32_t magic) {
    auto* h = static_cast<Handle*>(handle);
    if (!h || (reinterpret_cast<uintptr_t>(h) & (kHandleAlign - 1))) return nullptr;
    return h->self == h && h->magic == magic ? h : nullptr;
}

// ---- decoder ----------------------------------------------------------------

struct Header {
    uint32_t width = 0, height = 0;
    uint32_t components = 0;
    bool progressive = false;
};

uint32_t Be16(const uint8_t* p) { return (uint32_t{p[0]} << 8) | p[1]; }

bool IsFrameHeader(uint8_t m) { return m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC; }

// Walks the marker segments from SOI to the first scan (SOS), never reading
// past `size`, and returns the frame header's contents. Takes the frames
// stb_image decodes: 8-bit baseline, extended sequential and progressive
// Huffman, with 1, 3 or 4 components. Bytes between segments are skipped, as
// stb does. A frame without a scan is refused here, because stb would hand back
// an image no scan ever wrote.
int32_t ParseHeaderSegments(const uint8_t* data, uint32_t size, Header& out) {
    if (size < 4 || data[0] != 0xFF || data[1] != 0xD8) return kDecErrorInvalidData;
    bool haveFrame = false;
    uint32_t pos = 2;
    for (;;) {
        while (pos < size && data[pos] != 0xFF) ++pos;
        while (pos < size && data[pos] == 0xFF) ++pos;
        if (pos >= size) return kDecErrorInvalidData;  // ran out before a scan
        const uint8_t m = data[pos++];
        if (m == 0x00 || m == 0x01 || m == 0xD8 || (m >= 0xD0 && m <= 0xD7)) continue;  // no length
        if (m == 0xD9) return kDecErrorInvalidData;  // ended before a scan
        if (size - pos < 2) return kDecErrorInvalidData;
        const uint32_t len = Be16(data + pos);
        if (len < 2 || len > size - pos) return kDecErrorInvalidData;
        if (m == 0xDA) return haveFrame ? kernel::SCE_OK : kDecErrorInvalidData;
        if (IsFrameHeader(m)) {
            if (haveFrame) return kDecErrorInvalidData;  // two frame headers
            if (len < 8) return kDecErrorInvalidData;
            const uint8_t* f = data + pos + 2;  // precision, height, width, component count
            out.height = Be16(f + 1);
            out.width = Be16(f + 3);
            out.components = f[5];
            if (len < 8 + 3 * out.components) return kDecErrorInvalidData;
            out.progressive = m == 0xC2;
            if (m != 0xC0 && m != 0xC1 && m != 0xC2) return kDecErrorUnsupportedData;  // lossless, arithmetic
            if (f[0] != 8) return kDecErrorUnsupportedData;                              // 12-bit samples
            if (out.components != 1 && out.components != 3 && out.components != 4) return kDecErrorUnsupportedData;
            if (out.width == 0 || out.height == 0) return kDecErrorUnsupportedData;  // height set by DNL
            if (out.width > kDecMaxWidth) return kDecErrorUnsupportedData;
            haveFrame = true;
        }
        pos += len;
    }
}

void FillInfo(const Header& h, DecImageInfo* info) {
    std::memset(info, 0, sizeof(*info));
    info->imageWidth = h.width;
    info->imageHeight = h.height;
    // stb needs no extra buffer, even for progressive images, and the game
    // skips allocating one when this is 0.
    info->extraBufferSize = 0;
    info->createAttr = 0;
    info->outputWidth = h.width;
    info->outputHeight = h.height;
}

void NoteOptions(uint16_t option0, uint16_t option1) {
    static std::atomic<bool> noted{false};
    if ((option0 != 0 || option1 != 1) && !noted.exchange(true)) {
        std::fprintf(stderr, "[jpeg] decode options %u/%u (the game uses 0/1); decoding at full size\n",
                     option0, option1);
    }
}

// ---- encoder ----------------------------------------------------------------

// Follows shadPS4's checks, with the size arithmetic done in 64 bits.
int32_t ValidateEncode(const EncEncodeParam* p) {
    if (!p || !p->image || !p->jpeg) return kEncErrorInvalidAddr;
    if (p->pixelFormat != kEncY8 && (reinterpret_cast<uintptr_t>(p->image) & 3)) return kEncErrorInvalidAddr;
    if (p->imageSize == 0 || p->jpegSize == 0) return kEncErrorInvalidSize;
    if (p->width == 0 || p->height == 0 || p->width > kEncMaxDimension || p->height > kEncMaxDimension) {
        return kEncErrorInvalidParam;
    }
    if (p->pitch == 0 || p->pitch > kEncMaxPitch || (p->pixelFormat != kEncY8 && (p->pitch & 3))) {
        return kEncErrorInvalidParam;
    }
    const uint64_t needed = uint64_t{p->pitch} * p->height;
    if (needed > kEncMaxImageSize || needed > p->imageSize) return kEncErrorInvalidParam;
    if (p->mode != kEncModeNormal && p->mode != kEncModeMjpeg) return kEncErrorInvalidParam;
    if (p->colorSpace != kEncYcc && p->colorSpace != kEncGrayscale) return kEncErrorInvalidParam;
    if (p->sampling > kEncSampling420) return kEncErrorInvalidParam;
    if (p->restartInterval > static_cast<int32_t>(kEncMaxDimension)) return kEncErrorInvalidParam;
    switch (p->pixelFormat) {
    case kEncR8G8B8A8:
    case kEncB8G8R8A8:
        if (p->pitch / 4 < p->width || p->colorSpace != kEncYcc || p->sampling == kEncSamplingFull) {
            return kEncErrorInvalidParam;
        }
        break;
    case kEncY8U8Y8V8:
        if (p->pitch / 2 < ((p->width + 1) & ~1u) || p->colorSpace != kEncYcc || p->sampling == kEncSamplingFull) {
            return kEncErrorInvalidParam;
        }
        break;
    case kEncY8:
        if (p->pitch < p->width || p->colorSpace != kEncGrayscale || p->sampling != kEncSamplingFull) {
            return kEncErrorInvalidParam;
        }
        break;
    default:
        return kEncErrorInvalidParam;
    }
    return kernel::SCE_OK;
}

uint8_t Clamp8(float v) { return static_cast<uint8_t>(std::clamp(v + 0.5f, 0.0f, 255.0f)); }

// Converts the game's pixels to the tightly packed RGB (or grey) rows stb takes.
void ToPacked(const EncEncodeParam& p, int comp, uint8_t* out) {
    const auto* src = static_cast<const uint8_t*>(p.image);
    for (uint32_t y = 0; y < p.height; ++y) {
        const uint8_t* row = src + size_t{p.pitch} * y;
        uint8_t* o = out + size_t{p.width} * static_cast<size_t>(comp) * y;
        switch (p.pixelFormat) {
        case kEncR8G8B8A8:
            for (uint32_t x = 0; x < p.width; ++x, o += 3) std::memcpy(o, row + 4 * x, 3);
            break;
        case kEncB8G8R8A8:
            for (uint32_t x = 0; x < p.width; ++x, o += 3) {
                o[0] = row[4 * x + 2];
                o[1] = row[4 * x + 1];
                o[2] = row[4 * x];
            }
            break;
        case kEncY8U8Y8V8:
            // Y0 U Y1 V for each pixel pair; JFIF (full-range BT.601) conversion.
            for (uint32_t x = 0; x < p.width; ++x, o += 3) {
                const uint8_t* pair = row + 4 * (x / 2);
                const float yv = pair[(x & 1) ? 2 : 0], u = pair[1] - 128.0f, v = pair[3] - 128.0f;
                o[0] = Clamp8(yv + 1.402f * v);
                o[1] = Clamp8(yv - 0.344136f * u - 0.714136f * v);
                o[2] = Clamp8(yv + 1.772f * u);
            }
            break;
        default:  // kEncY8
            std::memcpy(o, row, p.width);
            break;
        }
    }
}

// Collects stb's output in the game's buffer, refusing to go past its end.
struct Sink {
    uint8_t* out;
    uint32_t capacity;
    uint32_t used;
    bool overflow;
};

void SinkWrite(void* context, void* data, int size) {
    auto* s = static_cast<Sink*>(context);
    if (s->overflow || size <= 0) return;
    if (uint64_t{s->used} + static_cast<uint32_t>(size) > s->capacity) {
        s->overflow = true;
        return;
    }
    std::memcpy(s->out + s->used, data, static_cast<size_t>(size));
    s->used += static_cast<uint32_t>(size);
}

const char* EncFormatName(uint16_t f) {
    switch (f) {
    case kEncR8G8B8A8: return "RGBA";
    case kEncB8G8R8A8: return "BGRA";
    case kEncY8U8Y8V8: return "YUYV";
    default: return "Y8";
    }
}

}  // namespace

// ---- libSceJpegDec ----------------------------------------------------------

int32_t PS4_SYSV_ABI sceJpegDecParseHeader(const DecParseParam* param, DecImageInfo* info) {
    // The game sizes a buffer from extraBufferSize before it checks the result,
    // so the output is cleared even when parsing fails.
    if (info) std::memset(info, 0, sizeof(*info));
    if (!param || !info || !param->jpegData) return Fail(kDecErrorInvalidAddr, "ParseHeader: null address");
    if (param->jpegSize == 0) return Fail(kDecErrorInvalidSize, "ParseHeader: empty input");
    NoteOptions(param->option0, param->option1);
    Header h;
    if (const int32_t rc = ParseHeaderSegments(static_cast<const uint8_t*>(param->jpegData), param->jpegSize, h)) {
        return Fail(rc, "ParseHeader: no usable header in %u bytes", param->jpegSize);
    }
    FillInfo(h, info);
    return kernel::SCE_OK;
}

int32_t PS4_SYSV_ABI sceJpegDecQueryMemorySize(const DecCreateParam* param) {
    if (!param) return Fail(kDecErrorInvalidAddr, "Dec QueryMemorySize: null param");
    if (param->size != sizeof(DecCreateParam)) {
        return Fail(kDecErrorInvalidSize, "Dec QueryMemorySize: param size %u", param->size);
    }
    return static_cast<int32_t>(kMemorySize);
}

int32_t PS4_SYSV_ABI sceJpegDecCreate(const DecCreateParam* param, void* memory, uint32_t memorySize,
                                      DecHandle* handle) {
    // A failed QueryMemorySize looks like a huge size to the game, whose
    // allocation then fails: Create sees (null, 0) and has to refuse it.
    if (!param || !memory || !handle) return Fail(kDecErrorInvalidAddr, "DecCreate: null address");
    if (param->size != sizeof(DecCreateParam)) {
        return Fail(kDecErrorInvalidSize, "DecCreate: param size %u", param->size);
    }
    if (param->maxWidth == 0) return Fail(kDecErrorInvalidParam, "DecCreate: maxWidth 0");
    if (memorySize < kMemorySize || !MakeHandle(memory, memorySize, kDecMagic, param->maxWidth, handle)) {
        return Fail(kDecErrorInvalidSize, "DecCreate: %u bytes of memory", memorySize);
    }
    return kernel::SCE_OK;
}

int32_t PS4_SYSV_ABI sceJpegDecDecode(DecHandle handle, const DecDecodeParam* param, DecImageInfo* info) {
    Handle* h = LiveHandle(handle, kDecMagic);
    if (!h) return Fail(kDecErrorInvalidHandle, "Decode: bad handle %p", handle);
    if (!param || !param->jpegData || !param->image) return Fail(kDecErrorInvalidAddr, "Decode: null address");
    if (param->jpegSize == 0 || param->imageSize == 0) return Fail(kDecErrorInvalidSize, "Decode: zero size");
    if (param->pixelFormat != kDecR8G8B8A8 && param->pixelFormat != kDecB8G8R8A8) {
        return Fail(kDecErrorInvalidParam, "Decode: pixel format %u", param->pixelFormat);
    }
    NoteOptions(param->option0, param->option1);

    const auto* data = static_cast<const uint8_t*>(param->jpegData);
    Header hdr;
    if (const int32_t rc = ParseHeaderSegments(data, param->jpegSize, hdr)) {
        return Fail(rc, "Decode: no usable header in %u bytes", param->jpegSize);
    }
    if (hdr.width > h->maxWidth) {
        return Fail(kDecErrorInvalidParam, "Decode: %ux%u is wider than the handle's %u", hdr.width, hdr.height,
                    h->maxWidth);
    }
    const uint32_t rowBytes = hdr.width * 4;
    const uint32_t pitch = param->pitch ? param->pitch : rowBytes;
    if (pitch < rowBytes) return Fail(kDecErrorInvalidParam, "Decode: pitch %u for width %u", pitch, hdr.width);
    const uint64_t needed = uint64_t{pitch} * (hdr.height - 1) + rowBytes;
    if (needed > param->imageSize) {
        return Fail(kDecErrorInvalidSize, "Decode: %ux%u needs %llu bytes, the buffer has %u", hdr.width,
                    hdr.height, static_cast<unsigned long long>(needed), param->imageSize);
    }
    if (param->jpegSize > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return Fail(kDecErrorUnsupportedData, "Decode: %u bytes of input", param->jpegSize);
    }

    int w = 0, hgt = 0, comp = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> rgb(
        stbi_load_from_memory(data, static_cast<int>(param->jpegSize), &w, &hgt, &comp, 3), &stbi_image_free);
    if (!rgb) return Fail(kDecErrorDecode, "Decode: stb_image: %s", stbi_failure_reason());
    if (static_cast<uint32_t>(w) != hdr.width || static_cast<uint32_t>(hgt) != hdr.height) {
        return Fail(kDecErrorDecode, "Decode: stb_image read %dx%d, the header says %ux%u", w, hgt, hdr.width,
                    hdr.height);
    }

    const bool bgra = param->pixelFormat == kDecB8G8R8A8;
    const auto alpha = static_cast<uint8_t>(std::min<uint16_t>(param->alpha, 0xFF));
    auto* out = static_cast<uint8_t*>(param->image);
    const stbi_uc* src = rgb.get();
    for (uint32_t y = 0; y < hdr.height; ++y) {
        uint8_t* d = out + uint64_t{pitch} * y;
        for (uint32_t x = 0; x < hdr.width; ++x, src += 3, d += 4) {
            d[0] = bgra ? src[2] : src[0];
            d[1] = src[1];
            d[2] = bgra ? src[0] : src[2];
            d[3] = alpha;
        }
    }
    if (info) FillInfo(hdr, info);
    if (ShouldLog(g_decLogs)) {
        std::fprintf(stderr, "[jpeg] decoded %ux%u%s (%u bytes, %d components) to %s, pitch %u\n", hdr.width,
                     hdr.height, hdr.progressive ? " progressive" : "", param->jpegSize, comp,
                     bgra ? "BGRA" : "RGBA", pitch);
    }
    return static_cast<int32_t>((hdr.width << 16) | hdr.height);
}

int32_t PS4_SYSV_ABI sceJpegDecDelete(DecHandle handle) {
    Handle* h = LiveHandle(handle, kDecMagic);
    if (!h) return Fail(kDecErrorInvalidHandle, "DecDelete: bad handle %p", handle);
    h->self = nullptr;
    h->magic = 0;
    return kernel::SCE_OK;
}

// ---- libSceJpegEnc ----------------------------------------------------------

int32_t PS4_SYSV_ABI sceJpegEncQueryMemorySize(const EncCreateParam* param) {
    if (!param) return Fail(kEncErrorInvalidAddr, "Enc QueryMemorySize: null param");
    if (param->size != sizeof(EncCreateParam)) {
        return Fail(kEncErrorInvalidSize, "Enc QueryMemorySize: param size %u", param->size);
    }
    if (param->attr != 0) return Fail(kEncErrorInvalidParam, "Enc QueryMemorySize: attr %u", param->attr);
    return static_cast<int32_t>(kMemorySize);
}

int32_t PS4_SYSV_ABI sceJpegEncCreate(const EncCreateParam* param, void* memory, uint32_t memorySize,
                                      EncHandle* handle) {
    if (!param || !memory || !handle) return Fail(kEncErrorInvalidAddr, "EncCreate: null address");
    if (param->size != sizeof(EncCreateParam)) {
        return Fail(kEncErrorInvalidSize, "EncCreate: param size %u", param->size);
    }
    if (param->attr != 0) return Fail(kEncErrorInvalidParam, "EncCreate: attr %u", param->attr);
    if (memorySize < kMemorySize || !MakeHandle(memory, memorySize, kEncMagic, 0, handle)) {
        return Fail(kEncErrorInvalidSize, "EncCreate: %u bytes of memory", memorySize);
    }
    return kernel::SCE_OK;
}

int32_t PS4_SYSV_ABI sceJpegEncEncode(EncHandle handle, const EncEncodeParam* param, EncOutputInfo* outputInfo) {
    if (!LiveHandle(handle, kEncMagic)) return Fail(kEncErrorInvalidHandle, "Encode: bad handle %p", handle);
    if (const int32_t rc = ValidateEncode(param)) {
        if (!param) return Fail(rc, "Encode: null param");
        return Fail(rc, "Encode: refused %ux%u pitch %u format %u space %u sampling %u (image %u, jpeg %u bytes)",
                    param->width, param->height, param->pitch, param->pixelFormat, param->colorSpace,
                    param->sampling, param->imageSize, param->jpegSize);
    }
    const EncEncodeParam& p = *param;
    // stb always writes 3-component YCbCr (grey input gives neutral chroma),
    // subsamples chroma 4:2:0 up to quality 90 and keeps it whole above, and
    // writes no restart markers. A decoder reads the same image either way.
    const int comp = p.pixelFormat == kEncY8 ? 1 : 3;
    const uint64_t packedSize = uint64_t{p.width} * p.height * static_cast<uint32_t>(comp);
    if (packedSize > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Fail(kEncErrorInvalidParam, "Encode: %ux%u is too large", p.width, p.height);
    }
    std::unique_ptr<uint8_t[]> packed(new (std::nothrow) uint8_t[packedSize]);
    if (!packed) return Fail(kEncErrorInvalidSize, "Encode: out of memory for %ux%u", p.width, p.height);
    ToPacked(p, comp, packed.get());

    const int quality = std::clamp<int>(p.quality, 1, 100);
    Sink sink{static_cast<uint8_t*>(p.jpeg), p.jpegSize, 0, false};
    const int ok = stbi_write_jpg_to_func(SinkWrite, &sink, static_cast<int>(p.width), static_cast<int>(p.height),
                                          comp, packed.get(), quality);
    if (sink.overflow) {
        return Fail(kEncErrorInvalidSize, "Encode: %ux%u at quality %d does not fit in %u bytes", p.width,
                    p.height, quality, p.jpegSize);
    }
    if (!ok || sink.used == 0) return Fail(kEncErrorInvalidParam, "Encode: stb_image_write failed");

    if (outputInfo) {
        outputInfo->size = sink.used;
        outputInfo->height = p.height;
    }
    if (ShouldLog(g_encLogs)) {
        std::fprintf(stderr, "[jpeg] encoded %ux%u %s, quality %d -> %u bytes (buffer %u)\n", p.width, p.height,
                     EncFormatName(p.pixelFormat), quality, sink.used, p.jpegSize);
    }
    return static_cast<int32_t>(sink.used);
}

int32_t PS4_SYSV_ABI sceJpegEncDelete(EncHandle handle) {
    Handle* h = LiveHandle(handle, kEncMagic);
    if (!h) return Fail(kEncErrorInvalidHandle, "EncDelete: bad handle %p", handle);
    h->self = nullptr;
    h->magic = 0;
    return kernel::SCE_OK;
}

}  // namespace hle::jpeg

namespace hle {

void RegisterJpeg(core::Linker& linker) {
    using namespace jpeg;
    const char* dec = "libSceJpegDec";
    HLE_REG(linker, dec, sceJpegDecParseHeader);
    HLE_REG(linker, dec, sceJpegDecQueryMemorySize);
    HLE_REG(linker, dec, sceJpegDecCreate);
    HLE_REG(linker, dec, sceJpegDecDecode);
    HLE_REG(linker, dec, sceJpegDecDelete);
    const char* enc = "libSceJpegEnc";
    HLE_REG(linker, enc, sceJpegEncQueryMemorySize);
    HLE_REG(linker, enc, sceJpegEncCreate);
    HLE_REG(linker, enc, sceJpegEncEncode);
    HLE_REG(linker, enc, sceJpegEncDelete);
}

}  // namespace hle
