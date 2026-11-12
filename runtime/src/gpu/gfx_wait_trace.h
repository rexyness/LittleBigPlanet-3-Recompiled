// LBP3_GFX_WAIT_TRACE=1 (milestone 5 measurement, lbp3-pc perf/gfx_wait.md): the guest threads'
// sleeps and blocking waits, as rows in shadPS4's analysis\gfx_wait_trace.csv (see
// third_party/shadPS4/src/video_core/amdgpu/lbp3_gfx_wait_trace.h). Measurement only.
//
//   BW t flip tid fn dur_us req_us caller   a guest sleep or blocking wait that took 20 us or
//                                            more (req_us: the sleep or timeout asked for, -1
//                                            none; caller: the game code that called)
//   TN tid name                              a thread's name, before its first BW row
#pragma once

#include <chrono>
#include <cstdint>

namespace gpu::gfxwait {

bool On();
void GuestWait(const char* fn, int64_t begin_ns, int64_t end_ns, int64_t req_us, void* caller);

inline int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Scope {
    const char* fn;
    int64_t req_us;
    void* caller;
    bool on;
    int64_t t0 = 0;
    Scope(const char* fn_, int64_t req, void* caller_) : fn(fn_), req_us(req), caller(caller_), on(On()) {
        if (on) t0 = NowNs();
    }
    ~Scope() {
        if (on) GuestWait(fn, t0, NowNs(), req_us, caller);
    }
};

}  // namespace gpu::gfxwait

#define LBP3_GFXWAIT_SCOPE(req_us) \
    gpu::gfxwait::Scope lbp3_gfxwait_scope(__func__, static_cast<int64_t>(req_us), __builtin_return_address(0))
