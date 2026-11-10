// libSceJpegDec and libSceJpegEnc: structures and entry points, shared with
// tests/jpegtest.cpp. Layouts are the ones the game's own call sites fill and
// read (eboot.bin+0x90f100 for the decoder, +0x412450 for the encoder),
// cross-checked against OpenOrbis' include/orbis/_types/jpeg.h and, for the
// encoder, shadPS4's jpegenc.h. Notes: .scratch/lbp3-port/m2/jpeg.md.
#pragma once
#include <cstddef>
#include <cstdint>

#include "hle/abi.h"

namespace hle::jpeg {

// ---- libSceJpegDec ----------------------------------------------------------

struct DecParseParam {
    const void* jpegData;
    uint32_t jpegSize;
    // The game passes 0 and 1 and copies both, unchanged, into the decode
    // parameters. Their meaning is not established (a decode mode and a
    // downscale factor where 1 means full size would fit); any value decodes
    // at full size here.
    uint16_t option0;
    uint16_t option1;
};
static_assert(sizeof(DecParseParam) == 0x10);

struct DecImageInfo {
    uint32_t imageWidth;   // the game allocates imageWidth * imageHeight * 4 bytes for the pixels
    uint32_t imageHeight;
    uint8_t unknown08[12];  // colour space, sampling factors (OpenOrbis); the game never reads them
    uint32_t extraBufferSize;  // the game allocates this much and passes it to Decode (+0x10, +0x20)
    uint32_t createAttr;       // the game copies this into DecCreateParam::attr
    uint32_t outputWidth;      // the game copies this into DecCreateParam::maxWidth
    uint32_t outputHeight;
};
static_assert(sizeof(DecImageInfo) == 0x24);
static_assert(offsetof(DecImageInfo, extraBufferSize) == 0x14);
static_assert(offsetof(DecImageInfo, createAttr) == 0x18);
static_assert(offsetof(DecImageInfo, outputWidth) == 0x1c);

struct DecCreateParam {
    uint32_t size;  // sizeof(DecCreateParam)
    uint32_t attr;
    uint32_t maxWidth;
};
static_assert(sizeof(DecCreateParam) == 0xc);

enum DecPixelFormat : uint16_t { kDecR8G8B8A8 = 0, kDecB8G8R8A8 = 1 };

struct DecDecodeParam {
    const void* jpegData;
    void* image;
    void* extraBuffer;  // the DecImageInfo::extraBufferSize buffer, or null
    uint32_t jpegSize;
    uint32_t imageSize;
    uint32_t extraBufferSize;
    uint16_t option0;  // from DecParseParam
    uint16_t option1;
    uint16_t pixelFormat;  // DecPixelFormat
    uint16_t alpha;        // written to every pixel's alpha byte
    uint32_t pitch;        // bytes per output row; 0 = imageWidth * 4
};
static_assert(sizeof(DecDecodeParam) == 0x30);
static_assert(offsetof(DecDecodeParam, jpegSize) == 0x18);
static_assert(offsetof(DecDecodeParam, option0) == 0x24);
static_assert(offsetof(DecDecodeParam, pixelFormat) == 0x28);
static_assert(offsetof(DecDecodeParam, alpha) == 0x2a);
static_assert(offsetof(DecDecodeParam, pitch) == 0x2c);

using DecHandle = void*;

int32_t PS4_SYSV_ABI sceJpegDecParseHeader(const DecParseParam* param, DecImageInfo* info);
int32_t PS4_SYSV_ABI sceJpegDecQueryMemorySize(const DecCreateParam* param);
int32_t PS4_SYSV_ABI sceJpegDecCreate(const DecCreateParam* param, void* memory, uint32_t memorySize,
                                      DecHandle* handle);
// Returns (outputWidth << 16) | outputHeight on success, a negative error otherwise.
int32_t PS4_SYSV_ABI sceJpegDecDecode(DecHandle handle, const DecDecodeParam* param, DecImageInfo* info);
int32_t PS4_SYSV_ABI sceJpegDecDelete(DecHandle handle);

// ---- libSceJpegEnc ----------------------------------------------------------

struct EncCreateParam {
    uint32_t size;  // sizeof(EncCreateParam)
    uint32_t attr;  // must be 0
};
static_assert(sizeof(EncCreateParam) == 0x8);

enum EncPixelFormat : uint16_t {
    kEncR8G8B8A8 = 0,
    kEncB8G8R8A8 = 1,
    kEncY8U8Y8V8 = 10,
    kEncY8 = 11,
};
enum EncMode : uint16_t { kEncModeNormal = 0, kEncModeMjpeg = 1 };
enum EncColorSpace : uint16_t { kEncYcc = 1, kEncGrayscale = 2 };
enum EncSampling : uint8_t { kEncSamplingFull = 0, kEncSampling422 = 1, kEncSampling420 = 2 };

struct EncEncodeParam {
    const void* image;
    void* jpeg;
    uint32_t imageSize;
    uint32_t jpegSize;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;  // bytes
    uint16_t pixelFormat;  // EncPixelFormat
    uint16_t mode;         // EncMode
    uint16_t colorSpace;   // EncColorSpace
    uint8_t sampling;      // EncSampling
    uint8_t quality;       // "compression ratio"; the game passes 50, 70 or 90
    int32_t restartInterval;
};
static_assert(sizeof(EncEncodeParam) == 0x30);
static_assert(offsetof(EncEncodeParam, pitch) == 0x20);
static_assert(offsetof(EncEncodeParam, pixelFormat) == 0x24);
static_assert(offsetof(EncEncodeParam, sampling) == 0x2a);
static_assert(offsetof(EncEncodeParam, quality) == 0x2b);
static_assert(offsetof(EncEncodeParam, restartInterval) == 0x2c);

struct EncOutputInfo {
    uint32_t size;
    uint32_t height;
};
static_assert(sizeof(EncOutputInfo) == 0x8);

using EncHandle = void*;

int32_t PS4_SYSV_ABI sceJpegEncQueryMemorySize(const EncCreateParam* param);
int32_t PS4_SYSV_ABI sceJpegEncCreate(const EncCreateParam* param, void* memory, uint32_t memorySize,
                                      EncHandle* handle);
// Returns the JPEG's size in bytes on success (the game stores it as the
// length), a negative error otherwise. outputInfo may be null.
int32_t PS4_SYSV_ABI sceJpegEncEncode(EncHandle handle, const EncEncodeParam* param, EncOutputInfo* outputInfo);
int32_t PS4_SYSV_ABI sceJpegEncDelete(EncHandle handle);

// Error codes. JpegEnc's are shadPS4's (jpeg_error.h). JpegDec's are inferred:
// libScePngDec numbers its errors 0x806900xx next to its encoder's 0x806901xx,
// so the JPEG decoder's are taken to be 0x806500xx with the same suffixes. The
// game only tests for zero or the sign.
constexpr int32_t kEncErrorInvalidAddr = static_cast<int32_t>(0x80650101);
constexpr int32_t kEncErrorInvalidSize = static_cast<int32_t>(0x80650102);
constexpr int32_t kEncErrorInvalidParam = static_cast<int32_t>(0x80650103);
constexpr int32_t kEncErrorInvalidHandle = static_cast<int32_t>(0x80650104);
constexpr int32_t kDecErrorInvalidAddr = static_cast<int32_t>(0x80650001);
constexpr int32_t kDecErrorInvalidSize = static_cast<int32_t>(0x80650002);
constexpr int32_t kDecErrorInvalidParam = static_cast<int32_t>(0x80650003);
constexpr int32_t kDecErrorInvalidHandle = static_cast<int32_t>(0x80650004);
constexpr int32_t kDecErrorInvalidData = static_cast<int32_t>(0x80650010);
constexpr int32_t kDecErrorUnsupportedData = static_cast<int32_t>(0x80650011);
constexpr int32_t kDecErrorDecode = static_cast<int32_t>(0x80650012);

}  // namespace hle::jpeg
