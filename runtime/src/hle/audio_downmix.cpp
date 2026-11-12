// 7.1 -> stereo downmix and the peak limiter (see audio_downmix.h).
#include "hle/audio_downmix.h"

#include <algorithm>
#include <cmath>

namespace hle::audio {

void Downmix71ToStereo(const float* in, float* out, size_t frames, const DownmixWeights& w) {
    for (size_t f = 0; f < frames; ++f, in += 8, out += 2) {
        const float fc = in[2] * w.center, lfe = in[3] * w.lfe;
        out[0] = in[0] + fc + lfe + in[4] * w.back + in[6] * w.side;
        out[1] = in[1] + fc + lfe + in[5] * w.back + in[7] * w.side;
    }
}

void Sdl71ToStereo(const float* in, float* out, size_t frames) {
    // SDL_audio_channel_converters.h, SDL_Convert71ToStereo (SDL 3.x).
    for (size_t f = 0; f < frames; ++f, in += 8, out += 2) {
        const float fc = in[2], lfe = in[3], bl = in[4], br = in[5], sl = in[6], sr = in[7];
        out[0] = in[0] * 0.211866662f + fc * 0.150266662f + lfe * 0.066666670f + bl * 0.181066677f +
                 br * 0.111066669f + sl * 0.194133341f + sr * 0.085866667f;
        out[1] = in[1] * 0.211866662f + fc * 0.150266662f + lfe * 0.066666670f + bl * 0.111066669f +
                 br * 0.181066677f + sl * 0.085866667f + sr * 0.194133341f;
    }
}

void Limiter::Process(float* x, size_t frames, int rate) {
    const size_t n = frames * 2;
    float peak = 0;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, std::fabs(x[i]));

    const float start = gain;
    float end;
    if (peak * start > threshold) {
        end = threshold / peak;  // attack: down to the threshold for this buffer
    } else {
        // Release towards 1 (tau 150 ms), but never so far that this buffer's peak goes over.
        const double dt = static_cast<double>(frames) / (rate > 0 ? rate : 48000);
        end = static_cast<float>(1.0 - (1.0 - start) * std::exp(-dt / 0.150));
        if (peak > 0) end = std::min(end, threshold / peak);
        end = std::max(end, start);
    }
    if (start >= 1.0f && end >= 1.0f) return;  // the common case: nothing to do

    // Attack ramps in over the first millisecond; release ramps across the whole buffer.
    const size_t ramp = end < start ? std::min<size_t>(frames, static_cast<size_t>((rate > 0 ? rate : 48000) / 1000))
                                    : frames;
    const float knee = 1.0f - threshold;
    for (size_t f = 0; f < frames; ++f) {
        const float g = f < ramp ? start + (end - start) * (static_cast<float>(f + 1) / ramp) : end;
        for (int c = 0; c < 2; ++c) {
            float v = x[f * 2 + c] * g;
            const float a = std::fabs(v);
            if (a > threshold) {
                v = std::copysign(threshold + knee * std::tanh((a - threshold) / knee), v);
                ++softClipped;
            }
            x[f * 2 + c] = v;
        }
    }
    gain = std::min(end, 1.0f);
    minGain = std::min(minGain, std::min(start, end));
    if (std::min(start, end) < 1.0f) ++reducedBuffers;
}

void Level::Add(const float* x, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        sumSq += static_cast<double>(x[i]) * x[i];
        peak = std::max(peak, std::fabs(x[i]));
    }
    samples += n;
}

double Level::RmsDb() const {
    if (!samples || sumSq <= 0) return -999.0;
    return 10.0 * std::log10(sumSq / samples);
}

double Level::PeakDb() const { return peak > 0 ? 20.0 * std::log10(peak) : -999.0; }

}  // namespace hle::audio
