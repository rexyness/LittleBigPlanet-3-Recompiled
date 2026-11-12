// The upscaler's size rules (LBP3_UPSCALE=fsr1; gpu/upscale.h), kept apart so the offline test
// can check them without a GPU.
#pragma once
#include <cstdint>

namespace gpu::upscale {

struct Size {
    uint32_t w = 0, h = 0;
    bool operator==(const Size& o) const { return w == o.w && h == o.h; }
    bool operator!=(const Size& o) const { return !(*this == o); }
    bool Empty() const { return w == 0 || h == 0; }
};

// The largest window the upscaler serves (bigger windows keep the window's own stretch).
constexpr uint32_t kMaxSide = 4096;

// The size the frame is upscaled to for a window of `window` pixels: the window's own size when
// it is bigger than the frame in at least one direction and not smaller in either. {0, 0} =
// don't upscale (a window at most the frame's size, smaller in one direction, minimised, or huge):
// the window then stretches the frame as before.
inline Size TargetFor(Size frame, Size window) {
    if (frame.Empty() || window.Empty()) return {};
    if (window.w < frame.w || window.h < frame.h) return {};
    if (window.w == frame.w && window.h == frame.h) return {};
    if (window.w > kMaxSide || window.h > kMaxSide) return {};
    return window;
}

// A new target takes effect once it has held for `frames` frames in a row, so dragging the
// window's edge doesn't reallocate on every frame. The first target takes effect at once.
struct Debounce {
    Size current, pending;
    int count = 0;
    int frames = 8;
    bool started = false;

    // The target to use this frame, given the one wanted now.
    Size Update(Size want) {
        if (!started) {
            started = true;
            current = pending = want;
            count = 0;
            return current;
        }
        if (want == current) {
            pending = current;
            count = 0;
            return current;
        }
        if (want != pending) {
            pending = want;
            count = 0;
        }
        if (++count >= frames) {
            current = pending;
            count = 0;
        }
        return current;
    }
};

}  // namespace gpu::upscale
