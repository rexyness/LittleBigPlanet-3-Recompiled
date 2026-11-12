// libSceAudioOut on SDL3 audio. Each PS4 port is an SDL audio stream bound to
// the default playback device; SDL converts sample rate and channel count
// (e.g. 7.1 game output on stereo speakers).
//
// sceAudioOutOutput blocks like on the console: the game's mixer (FMOD here)
// hands over one buffer per period and relies on the call to pace it in real
// time. We keep about two periods queued and wait while more than that is
// pending. LBP3_VOLUME=0..100 scales everything.
//
// 7.1 on a stereo device (ticket 16): LBP3's FMOD mixes to an 8-channel port, and SDL's
// own 7.1 -> stereo matrix puts the front pair at 0.212 (about -13.5 dB), so the game played
// far too quietly on stereo speakers and headphones. The runtime downmixes such ports itself
// (hle/audio_downmix.h: fronts 1, centre and surrounds 0.707, a limiter above -1 dBFS) and
// hands SDL plain stereo. LBP3_DOWNMIX=sdl brings back SDL's matrix. Devices with more than
// two channels still get SDL's conversion. An [audio] line every 5 s gives each port's level.
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hle/audio_downmix.h"
#include "hle/kernel_errors.h"
#include "hle/register.h"

namespace hle::audio {
namespace {

constexpr int kErrorNotOpened = static_cast<int>(0x80260001);
constexpr int kErrorInvalidPort = static_cast<int>(0x80260003);
constexpr int kErrorPortFull = static_cast<int>(0x80260005);
constexpr int kErrorInvalidSize = static_cast<int>(0x80260006);
constexpr int kErrorInvalidFormat = static_cast<int>(0x80260007);
constexpr int kErrorInvalidSampleFreq = static_cast<int>(0x80260008);
constexpr int kErrorNotInit = static_cast<int>(0x8026000F);
constexpr int kErrorSystemResource = static_cast<int>(0x80260011);

constexpr int kMaxPorts = 32;
constexpr float kVolume0dB = 32768.0f;
constexpr int kQueuedPeriods = 3;  // latency target: 3 game buffers (64 ms at LBP3's 1024 frames)

// OrbisAudioOutParamFormat (low byte of the param argument).
struct Format {
    int channels;
    bool isFloat;
    bool std8;  // "standard" 7.1 order (L R C LFE Lb Rb Ls Rs), same as SDL's
};
bool DecodeFormat(uint32_t param, Format& f) {
    switch (param & 0xFF) {
    case 0: f = {1, false, false}; return true;
    case 1: f = {2, false, false}; return true;
    case 2: f = {8, false, false}; return true;
    case 3: f = {1, true, false}; return true;
    case 4: f = {2, true, false}; return true;
    case 5: f = {8, true, false}; return true;
    case 6: f = {8, false, true}; return true;
    case 7: f = {8, true, true}; return true;
    default: return false;
    }
}

struct Port {
    bool open = false;
    int type = 0;
    uint32_t frames = 0, rate = 0;
    Format fmt{};
    SDL_AudioStream* stream = nullptr;
    std::vector<float> scratch;
    float gain = 1.0f;
    uint64_t outputs = 0, underruns = 0;
    // 8-channel port on a stereo device: the runtime's downmix (else SDL's).
    bool downmix = false;
    bool stereoDevice = false;  // 8-channel port on a stereo device (either downmix)
    std::vector<float> stereo, sdlStereo;
    Limiter limiter;
    // Levels for the [audio] line, reset every window (before the port volume).
    Level inLevel, sdlLevel, oursLevel, outLevel;
    uint64_t windowOutputs = 0;
    int volumeLogs = 0;
};

std::mutex g_mutex;
bool g_init = false;
SDL_AudioDeviceID g_device = 0;
int g_deviceChannels = 0;
Port g_ports[kMaxPorts];
std::chrono::steady_clock::time_point g_lastLevels{};
constexpr auto kLevelsEvery = std::chrono::seconds(5);

// LBP3_DOWNMIX=sdl: leave 7.1 -> stereo to SDL (the old path).
bool RuntimeDownmix() {
    const char* s = std::getenv("LBP3_DOWNMIX");
    return !(s && std::strcmp(s, "sdl") == 0);
}

float MasterVolume() {
    static const float v = [] {
        const char* s = std::getenv("LBP3_VOLUME");
        return s ? std::clamp(std::atoi(s), 0, 100) / 100.0f : 1.0f;
    }();
    return v;
}

bool OpenDevice() {
    if (g_device) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "[audio] SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    g_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!g_device) {
        std::fprintf(stderr, "[audio] no playback device: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{};
    SDL_GetAudioDeviceFormat(g_device, &spec, nullptr);
    g_deviceChannels = spec.channels;
    std::fprintf(stderr, "[audio] output device: %s (%d Hz, %d ch)\n", SDL_GetAudioDeviceName(g_device), spec.freq,
                 spec.channels);
    return true;
}

// One [audio] line: each port that played in the window, its level before the port volume.
// For an 8-channel port on a stereo device: what came in, what went to SDL, and what SDL's
// own downmix and the runtime's (before the limiter) would make of it.
void PrintLevels(double seconds) {
    std::string line;
    char buf[512];
    for (int i = 0; i < kMaxPorts; ++i) {
        Port& p = g_ports[i];
        if (!p.open || !p.windowOutputs) continue;
        if (p.stereoDevice) {
            std::snprintf(buf, sizeof(buf),
                          "%sport %d (8 ch, %s downmix): in rms %.1f pk %.1f; out rms %.1f pk %.1f; SDL's would be "
                          "rms %.1f pk %.1f, the runtime's rms %.1f pk %.1f; limiter %llu buffers, min %.1f dB, %llu "
                          "soft",
                          line.empty() ? "" : " | ", i + 1, p.downmix ? "runtime" : "SDL", p.inLevel.RmsDb(),
                          p.inLevel.PeakDb(), p.outLevel.RmsDb(), p.outLevel.PeakDb(), p.sdlLevel.RmsDb(),
                          p.sdlLevel.PeakDb(), p.oursLevel.RmsDb(), p.oursLevel.PeakDb(),
                          static_cast<unsigned long long>(p.limiter.reducedBuffers),
                          20.0 * std::log10(std::max(p.limiter.minGain, 1e-6f)),
                          static_cast<unsigned long long>(p.limiter.softClipped));
            p.limiter.reducedBuffers = p.limiter.softClipped = 0;
            p.limiter.minGain = p.limiter.gain;
        } else {
            std::snprintf(buf, sizeof(buf), "%sport %d (%d ch): rms %.1f pk %.1f", line.empty() ? "" : " | ", i + 1,
                          p.fmt.channels, p.inLevel.RmsDb(), p.inLevel.PeakDb());
        }
        line += buf;
        p.inLevel.Reset();
        p.sdlLevel.Reset();
        p.oursLevel.Reset();
        p.outLevel.Reset();
        p.windowOutputs = 0;
    }
    if (!line.empty()) std::fprintf(stderr, "[audio] levels over %.0f s, dBFS: %s\n", seconds, line.c_str());
}

Port* GetPort(int handle) {
    if (handle < 1 || handle > kMaxPorts) return nullptr;
    Port* p = &g_ports[handle - 1];
    return p->open ? p : nullptr;
}

// Converts one game buffer to interleaved float in SDL's channel order.
void Convert(const Port& p, const void* src, float* dst) {
    const size_t n = static_cast<size_t>(p.frames) * p.fmt.channels;
    if (p.fmt.isFloat) std::memcpy(dst, src, n * sizeof(float));
    else {
        const auto* s = static_cast<const int16_t*>(src);
        for (size_t i = 0; i < n; ++i) dst[i] = s[i] * (1.0f / 32768.0f);
    }
    if (p.fmt.channels == 8 && !p.fmt.std8) {
        // PS4 order L R C LFE Ls Rs Lb Rb -> SDL 7.1 order FL FR FC LFE BL BR SL SR.
        for (uint32_t f = 0; f < p.frames; ++f) {
            float* fr = dst + f * 8;
            std::swap(fr[4], fr[6]);
            std::swap(fr[5], fr[7]);
        }
    }
}

}  // namespace

int PS4_SYSV_ABI sceAudioOutInit() {
    std::lock_guard lock(g_mutex);
    g_init = true;
    OpenDevice();
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceAudioOutOpen(int32_t userId, int32_t type, int32_t index, uint32_t frames, uint32_t rate,
                                 uint32_t param) {
    (void)userId;
    (void)index;
    std::lock_guard lock(g_mutex);
    if (!g_init) return kErrorNotInit;
    Format fmt;
    if (!DecodeFormat(param, fmt)) return kErrorInvalidFormat;
    if (frames == 0 || frames > 2048 || frames % 256) return kErrorInvalidSize;
    if (rate != 48000) return kErrorInvalidSampleFreq;
    if (!OpenDevice()) return kErrorSystemResource;

    int slot = -1;
    for (int i = 0; i < kMaxPorts; ++i)
        if (!g_ports[i].open) { slot = i; break; }
    if (slot < 0) return kErrorPortFull;

    const bool stereoDevice = fmt.channels == 8 && g_deviceChannels == 2;
    const bool downmix = stereoDevice && RuntimeDownmix();
    const SDL_AudioSpec src{SDL_AUDIO_F32, downmix ? 2 : fmt.channels, static_cast<int>(rate)};
    SDL_AudioStream* stream = SDL_CreateAudioStream(&src, nullptr);
    if (!stream || !SDL_BindAudioStream(g_device, stream)) {
        std::fprintf(stderr, "[audio] stream creation failed: %s\n", SDL_GetError());
        if (stream) SDL_DestroyAudioStream(stream);
        return kErrorSystemResource;
    }
    Port& p = g_ports[slot];
    p = Port{};
    p.open = true;
    p.type = type;
    p.frames = frames;
    p.rate = rate;
    p.fmt = fmt;
    p.stream = stream;
    p.scratch.resize(static_cast<size_t>(frames) * fmt.channels);
    p.stereoDevice = stereoDevice;
    p.downmix = downmix;
    if (stereoDevice) {
        p.stereo.resize(static_cast<size_t>(frames) * 2);
        p.sdlStereo.resize(static_cast<size_t>(frames) * 2);
    }
    SDL_SetAudioStreamGain(stream, MasterVolume());
    std::fprintf(stderr, "[audio] port %d: type %d, %u frames, %d ch %s%s\n", slot + 1, type, frames, fmt.channels,
                 fmt.isFloat ? "float" : "s16",
                 downmix        ? ", downmixed to stereo by the runtime (LBP3_DOWNMIX=sdl: SDL's matrix)"
                 : stereoDevice ? ", downmixed to stereo by SDL (LBP3_DOWNMIX=sdl)"
                                : "");
    return slot + 1;
}

int PS4_SYSV_ABI sceAudioOutClose(int32_t handle) {
    std::lock_guard lock(g_mutex);
    Port* p = GetPort(handle);
    if (!p) return kErrorInvalidPort;
    SDL_DestroyAudioStream(p->stream);
    *p = Port{};
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceAudioOutOutput(int32_t handle, const void* buffer) {
    SDL_AudioStream* stream;
    size_t bytes;
    {
        std::lock_guard lock(g_mutex);
        Port* p = GetPort(handle);
        if (!p) return kErrorNotOpened;
        stream = p->stream;
        const std::vector<float>& out = p->downmix ? p->stereo : p->scratch;
        bytes = out.size() * sizeof(float);
        if (buffer) {
            // An empty queue here (after start-up) means the speaker ran dry: an audible gap.
            // Logged every 1000th time: the voice port (3) runs dry routinely between lines,
            // which the port's gap list found harmless (gap 11), and every 50th filled long logs.
            if (++p->outputs > 8 && SDL_GetAudioStreamQueued(stream) == 0 && (++p->underruns % 1000) == 1)
                std::fprintf(stderr, "[audio] port %d underrun (%llu so far)\n", handle,
                             static_cast<unsigned long long>(p->underruns));
            Convert(*p, buffer, p->scratch.data());
            p->inLevel.Add(p->scratch.data(), p->scratch.size());
            if (p->stereoDevice) {
                // Both downmixes are measured, whichever is played, for the [audio] line.
                Sdl71ToStereo(p->scratch.data(), p->sdlStereo.data(), p->frames);
                Downmix71ToStereo(p->scratch.data(), p->stereo.data(), p->frames);
                p->sdlLevel.Add(p->sdlStereo.data(), p->sdlStereo.size());
                p->oursLevel.Add(p->stereo.data(), p->stereo.size());
                if (p->downmix) {
                    p->limiter.Process(p->stereo.data(), p->frames, static_cast<int>(p->rate));
                    p->outLevel.Add(p->stereo.data(), p->stereo.size());
                } else {
                    p->outLevel.Add(p->sdlStereo.data(), p->sdlStereo.size());
                }
            }
            ++p->windowOutputs;
            SDL_PutAudioStreamData(stream, out.data(), static_cast<int>(bytes));
            const auto now = std::chrono::steady_clock::now();
            if (g_lastLevels == std::chrono::steady_clock::time_point{}) g_lastLevels = now;
            else if (now - g_lastLevels >= kLevelsEvery) {
                PrintLevels(std::chrono::duration<double>(now - g_lastLevels).count());
                g_lastLevels = now;
            }
        }
    }
    // Pace the caller: wait until the queue is back down to the latency target.
    const int target = static_cast<int>(bytes) * kQueuedPeriods;
    for (int i = 0; i < 200 && SDL_GetAudioStreamQueued(stream) > target; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return kernel::SCE_OK;
}

// `flag` has a bit per channel whose volume is given; values are 0..32768 (0 dB).
int PS4_SYSV_ABI sceAudioOutSetVolume(int32_t handle, uint32_t flag, const int32_t* vol) {
    std::lock_guard lock(g_mutex);
    Port* p = GetPort(handle);
    if (!p) return kErrorInvalidPort;
    if (!vol) return kernel::SCE_OK;
    float loudest = 0.0f;
    for (int ch = 0; ch < 8; ++ch)
        if (flag & (1u << ch)) loudest = std::max(loudest, vol[ch] / kVolume0dB);
    if (loudest != p->gain && p->volumeLogs < 20) {
        ++p->volumeLogs;
        std::fprintf(stderr, "[audio] port %d volume: flags 0x%x, gain %.3f (%.1f dB)\n", handle, flag, loudest,
                     loudest > 0 ? 20.0 * std::log10(loudest) : -999.0);
    }
    p->gain = loudest;
    SDL_SetAudioStreamGain(p->stream, loudest * MasterVolume());
    return kernel::SCE_OK;
}

int PortStreamChannels(int handle) {
    std::lock_guard lock(g_mutex);
    Port* p = GetPort(handle);
    if (!p) return 0;
    SDL_AudioSpec src{};
    return SDL_GetAudioStreamFormat(p->stream, &src, nullptr) ? src.channels : 0;
}

}  // namespace hle::audio

namespace hle {

void RegisterAudioOut(core::Linker& linker) {
    using namespace audio;
    const char* lib = "libSceAudioOut";
    HLE_REG(linker, lib, sceAudioOutInit);
    HLE_REG(linker, lib, sceAudioOutOpen);
    HLE_REG(linker, lib, sceAudioOutClose);
    HLE_REG(linker, lib, sceAudioOutOutput);
    HLE_REG(linker, lib, sceAudioOutSetVolume);
}

}  // namespace hle
