// 7.1 -> stereo downmix for stereo playback devices (ticket 16).
//
// LBP3's FMOD mixes to an 8-channel float port. Left to SDL, a stereo device gets SDL's
// normalised matrix (SDL_Convert71ToStereo: front 0.212, centre 0.150, ...), whose
// coefficients add up to 1 so it can never clip, but plain stereo content in the front pair
// comes out at 0.212, about -13.5 dB: the "hella low" audio. The runtime instead downmixes
// with the usual ITU-style weights (fronts at 1, centre and surrounds at 0.707, LFE dropped)
// and catches the peaks with a limiter that only acts above -1 dBFS.
//
// All buffers are interleaved float in SDL's 7.1 order: FL FR FC LFE BL BR SL SR.
#pragma once

#include <cstddef>
#include <cstdint>

namespace hle::audio {

struct DownmixWeights {
    float center = 0.70710678f;
    float side = 0.70710678f;
    float back = 0.70710678f;
    float lfe = 0.0f;
};

// out[2 * frames] = in[8 * frames] downmixed with `w` (no limiting).
void Downmix71ToStereo(const float* in, float* out, size_t frames, const DownmixWeights& w = {});

// What SDL 3's SDL_Convert71ToStereo makes of the same input (its matrix, copied), for the
// [audio] line's comparison with the old path.
void Sdl71ToStereo(const float* in, float* out, size_t frames);

// Peak limiter for interleaved stereo. Below the threshold it passes samples through
// untouched. A buffer whose peak goes over it is brought down to the threshold by a gain
// that ramps in over the first millisecond and recovers with a ~150 ms time constant; a soft
// knee above the threshold catches what the ramp misses, so the output never exceeds 1.0.
struct Limiter {
    float threshold = 0.89125094f;  // -1 dBFS
    float gain = 1.0f;              // current gain (1 = no reduction)
    float minGain = 1.0f;           // lowest gain since the stats were last taken
    uint64_t reducedBuffers = 0;    // buffers processed with gain < 1
    uint64_t softClipped = 0;       // samples that went through the soft knee
    void Process(float* stereo, size_t frames, int rate);
};

// Statistics of an interleaved buffer: sum of squares, sample count and absolute peak.
struct Level {
    double sumSq = 0;
    uint64_t samples = 0;
    float peak = 0;
    void Add(const float* x, size_t n);
    void Reset() { *this = Level{}; }
    double RmsDb() const;   // dBFS, -inf printed as -999
    double PeakDb() const;
};

// The stream's source channel count for a port handle (2 when the runtime downmixes an
// 8-channel port), 0 for a closed port. For the offline checks.
int PortStreamChannels(int handle);

}  // namespace hle::audio
