// Offline checks for the audio level fix (ticket 16):
// - the runtime's 7.1 -> stereo downmix against the expected weights (front 1, centre and
//   surrounds 0.707, LFE dropped), channel separation;
// - SDL's own matrix measured through a real SDL audio stream (the old path: front at 0.212,
//   about -13.5 dB) and the runtime's copy of it used for the [audio] line;
// - the limiter: untouched below -1 dBFS, never above 1.0, recovers after loud passages;
// - libSceAudioOut on SDL's dummy device: an 8-channel port reaches SDL as stereo, and as
//   8 channels again with LBP3_DOWNMIX=sdl; stereo and mono ports are left alone.
// Exit code 0 = all passed.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "hle/abi.h"
#include "hle/audio_downmix.h"

namespace hle::audio {
int PS4_SYSV_ABI sceAudioOutInit();
int PS4_SYSV_ABI sceAudioOutOpen(int32_t userId, int32_t type, int32_t index, uint32_t frames, uint32_t rate,
                                 uint32_t param);
int PS4_SYSV_ABI sceAudioOutClose(int32_t handle);
int PS4_SYSV_ABI sceAudioOutOutput(int32_t handle, const void* buffer);
}  // namespace hle::audio

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void Near(double got, double want, double tol, const char* what) {
    ++g_checks;
    if (std::fabs(got - want) > tol) {
        ++g_failures;
        std::printf("FAIL: %s: got %.6f, want %.6f (+-%.6f)\n", what, got, want, tol);
    }
}

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;

// 8-channel buffer (SDL order FL FR FC LFE BL BR SL SR) with a 440 Hz sine of `amp` on the
// channels whose bit is set in `mask`.
std::vector<float> Sine71(size_t frames, unsigned mask, float amp, size_t phase0 = 0) {
    std::vector<float> v(frames * 8, 0.0f);
    for (size_t f = 0; f < frames; ++f) {
        const float s = amp * static_cast<float>(std::sin(2 * kPi * 440.0 * (f + phase0) / kRate));
        for (int c = 0; c < 8; ++c)
            if (mask & (1u << c)) v[f * 8 + c] = s;
    }
    return v;
}

double Rms(const float* x, size_t n, size_t stride = 1, size_t offset = 0) {
    double s = 0;
    size_t k = 0;
    for (size_t i = offset; i < n; i += stride, ++k) s += static_cast<double>(x[i]) * x[i];
    return k ? std::sqrt(s / k) : 0;
}

float Peak(const std::vector<float>& x) {
    float p = 0;
    for (float v : x) p = std::max(p, std::fabs(v));
    return p;
}

enum : unsigned { FL = 1, FR = 2, FC = 4, LFE = 8, BL = 16, BR = 32, SL = 64, SR = 128 };

void DownmixChecks() {
    const size_t n = 4800;
    std::vector<float> out(n * 2);
    const double inRms = 0.5 / std::sqrt(2.0);

    auto in = Sine71(n, FL | FR, 0.5f);
    hle::audio::Downmix71ToStereo(in.data(), out.data(), n);
    Near(Rms(out.data(), out.size(), 2, 0), inRms, 1e-4, "front pair: left at unity");
    Near(Rms(out.data(), out.size(), 2, 1), inRms, 1e-4, "front pair: right at unity");

    in = Sine71(n, FC, 0.5f);
    hle::audio::Downmix71ToStereo(in.data(), out.data(), n);
    Near(Rms(out.data(), out.size(), 2, 0), inRms * 0.70710678, 1e-4, "centre: 0.707 in the left");
    Near(Rms(out.data(), out.size(), 2, 1), inRms * 0.70710678, 1e-4, "centre: 0.707 in the right");

    in = Sine71(n, SL, 0.5f);
    hle::audio::Downmix71ToStereo(in.data(), out.data(), n);
    Near(Rms(out.data(), out.size(), 2, 0), inRms * 0.70710678, 1e-4, "side left: 0.707 in the left");
    Near(Rms(out.data(), out.size(), 2, 1), 0.0, 1e-6, "side left: nothing in the right");

    in = Sine71(n, BR, 0.5f);
    hle::audio::Downmix71ToStereo(in.data(), out.data(), n);
    Near(Rms(out.data(), out.size(), 2, 1), inRms * 0.70710678, 1e-4, "back right: 0.707 in the right");
    Near(Rms(out.data(), out.size(), 2, 0), 0.0, 1e-6, "back right: nothing in the left");

    in = Sine71(n, FL, 0.5f);
    hle::audio::Downmix71ToStereo(in.data(), out.data(), n);
    Near(Rms(out.data(), out.size(), 2, 1), 0.0, 1e-6, "front left only: nothing in the right");

    in = Sine71(n, LFE, 0.5f);
    hle::audio::Downmix71ToStereo(in.data(), out.data(), n);
    Near(Rms(out.data(), out.size()), 0.0, 1e-6, "LFE only: dropped");
}

void SdlChecks() {
    // The old path, measured through SDL itself (the same library the runtime links).
    const SDL_AudioSpec src{SDL_AUDIO_F32, 8, kRate}, dst{SDL_AUDIO_F32, 2, kRate};
    SDL_AudioStream* s = SDL_CreateAudioStream(&src, &dst);
    Check(s != nullptr, "SDL 8 -> 2 channel stream created");
    if (!s) return;
    const size_t n = 4800;

    auto front = Sine71(n, FL | FR, 0.5f);
    SDL_PutAudioStreamData(s, front.data(), static_cast<int>(front.size() * sizeof(float)));
    SDL_FlushAudioStream(s);
    std::vector<float> got(n * 2);
    const int bytes = SDL_GetAudioStreamData(s, got.data(), static_cast<int>(got.size() * sizeof(float)));
    Check(bytes == static_cast<int>(got.size() * sizeof(float)), "SDL returned the whole buffer");
    const double ratio = Rms(got.data(), got.size()) / (0.5 / std::sqrt(2.0));
    Near(ratio, 0.211866662, 2e-3, "SDL puts the front pair at 0.212");
    std::printf("SDL 7.1 -> stereo, front pair: %.4f (%.1f dB); the runtime's downmix: 1.0000 (0.0 dB)\n", ratio,
                20 * std::log10(ratio));

    // The runtime's copy of SDL's matrix (used for the [audio] comparison) matches SDL on noise.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    std::vector<float> noise(n * 8);
    for (float& v : noise) v = u(rng);
    SDL_ClearAudioStream(s);
    SDL_PutAudioStreamData(s, noise.data(), static_cast<int>(noise.size() * sizeof(float)));
    SDL_FlushAudioStream(s);
    SDL_GetAudioStreamData(s, got.data(), static_cast<int>(got.size() * sizeof(float)));
    std::vector<float> mine(n * 2);
    hle::audio::Sdl71ToStereo(noise.data(), mine.data(), n);
    float maxDiff = 0;
    for (size_t i = 0; i < mine.size(); ++i) maxDiff = std::max(maxDiff, std::fabs(mine[i] - got[i]));
    Near(maxDiff, 0.0, 1e-4, "the runtime's copy of SDL's matrix matches SDL");
    SDL_DestroyAudioStream(s);
}

void LimiterChecks() {
    const size_t frames = 1024;
    // Below the threshold: bit-identical.
    {
        hle::audio::Limiter lim;
        std::vector<float> x(frames * 2), y;
        for (size_t f = 0; f < frames; ++f)
            x[f * 2] = x[f * 2 + 1] = 0.5f * static_cast<float>(std::sin(2 * kPi * 440.0 * f / kRate));
        y = x;
        for (int b = 0; b < 10; ++b) lim.Process(y.data(), frames, kRate);
        Check(y == x, "limiter: a -6 dBFS signal passes untouched");
        Check(lim.reducedBuffers == 0 && lim.softClipped == 0, "limiter: no reduction counted below the threshold");
    }
    // All eight channels at full scale: the raw downmix peaks at 1 + 3 x 0.707 = 3.12.
    {
        hle::audio::Limiter lim;
        float worst = 0, worstAfterFirst = 0;
        std::vector<float> out(frames * 2);
        for (int b = 0; b < 40; ++b) {
            auto in = Sine71(frames, 0xFF, 1.0f, b * frames);
            hle::audio::Downmix71ToStereo(in.data(), out.data(), frames);
            if (b == 0) Near(Peak(out), 1.0 + 3 * 0.70710678, 0.05, "full scale: raw downmix peaks at 1 + 3 x 0.707");
            lim.Process(out.data(), frames, kRate);
            worst = std::max(worst, Peak(out));
            if (b > 0) worstAfterFirst = std::max(worstAfterFirst, Peak(out));
        }
        Check(worst <= 1.0f, "limiter: never above 1.0 at full scale on all channels");
        Near(worstAfterFirst, lim.threshold, 2e-3, "limiter: held at -1 dBFS once engaged");
        Check(lim.reducedBuffers >= 39, "limiter: the loud buffers were reduced");
        // Then quiet for 1.5 s: the gain recovers.
        std::vector<float> quiet(frames * 2);
        for (size_t f = 0; f < frames; ++f)
            quiet[f * 2] = quiet[f * 2 + 1] = 0.1f * static_cast<float>(std::sin(2 * kPi * 440.0 * f / kRate));
        for (int b = 0; b < 70; ++b) {
            auto q = quiet;
            lim.Process(q.data(), frames, kRate);
        }
        Check(lim.gain > 0.99f, "limiter: back to unity gain after 1.5 s of quiet");
    }
}

void PortChecks() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    Check(hle::audio::sceAudioOutInit() == 0, "sceAudioOutInit");
    const int p8 = hle::audio::sceAudioOutOpen(1, 0, 0, 1024, 48000, 5);  // 8 ch float, like FMOD's main port
    const int p2 = hle::audio::sceAudioOutOpen(1, 0, 0, 1024, 48000, 1);  // 2 ch s16
    const int p1 = hle::audio::sceAudioOutOpen(1, 2, 0, 1024, 48000, 3);  // 1 ch float
    Check(p8 > 0 && p2 > 0 && p1 > 0, "ports opened");
    const int ch8 = hle::audio::PortStreamChannels(p8);
    std::printf("dummy device: 8-channel port reaches SDL with %d channels\n", ch8);
    // The dummy device is stereo by default; on any other layout the runtime leaves SDL to it.
    Check(ch8 == 2, "8-channel port on a stereo device: stereo into SDL (the runtime downmixes)");
    Check(hle::audio::PortStreamChannels(p2) == 2, "stereo port left alone");
    Check(hle::audio::PortStreamChannels(p1) == 1, "mono port left alone");
    auto buf = Sine71(1024, FL | FR, 0.5f);
    for (int i = 0; i < 4; ++i) Check(hle::audio::sceAudioOutOutput(p8, buf.data()) == 0, "8-channel output");
    hle::audio::sceAudioOutClose(p8);
    hle::audio::sceAudioOutClose(p2);
    hle::audio::sceAudioOutClose(p1);

    _putenv("LBP3_DOWNMIX=sdl");
    const int old = hle::audio::sceAudioOutOpen(1, 0, 0, 1024, 48000, 5);
    Check(hle::audio::PortStreamChannels(old) == 8, "LBP3_DOWNMIX=sdl: 8 channels into SDL again");
    Check(hle::audio::sceAudioOutOutput(old, buf.data()) == 0, "8-channel output on the old path");
    hle::audio::sceAudioOutClose(old);
    _putenv("LBP3_DOWNMIX=");
}

}  // namespace

int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) std::printf("note: SDL_Init(AUDIO) failed: %s\n", SDL_GetError());
    DownmixChecks();
    SdlChecks();
    LimiterChecks();
    PortChecks();
    std::printf("audiotest: %d checks, %d failed\n", g_checks, g_failures);
    SDL_Quit();
    return g_failures ? 1 : 0;
}
