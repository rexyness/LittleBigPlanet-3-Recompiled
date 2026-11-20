// See lbppc_mode.h.
#include "core/lbppc_mode.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace core::lbppc {
namespace {

// LBPPC's, measured on a running LBPPC 0.1.5 (2026-11-20, docs/research/lbppc-coop.md): every thread the
// game creates (JobManager and SceneThread workers, main fast/slow, streaming, HTTP...) runs MXCSR
// 0x1f80 (exceptions masked, no flush-to-zero, no denormals-are-zero) and x87 0x037f; none was seen
// with the console's 0x9fc0. So the only difference from ours is flush-to-zero and denormals-are-zero.
constexpr FpState kPs4{0x9fc0, 0x037f, true, "ps4"};
constexpr FpState kLbppc{0x1f80, 0x037f, false, "lbppc"};

}  // namespace

bool Mode() {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_LBPPC_COOP");
        const bool b = v && v[0] == '1';
        if (b) std::fprintf(stderr, "[lbppc] the LBPPC co-op mode is on (LBP3_LBPPC_COOP=1): online co-op with LBPPC players\n");
        return b;
    }();
    return on;
}

FpState GuestFp() {
    static const FpState s = [] {
        const char* v = std::getenv("LBP3_FP_STATE");
        FpState r = Mode() ? kLbppc : kPs4;
        if (v && *v) {
            if (!std::strcmp(v, "ps4")) {
                r = kPs4;
            } else if (!std::strcmp(v, "lbppc")) {
                r = kLbppc;
            } else {
                char* end = nullptr;
                const unsigned long mx = std::strtoul(v, &end, 16);
                unsigned long cw = 0x037f;
                if (end && *end == ':') cw = std::strtoul(end + 1, &end, 16);
                if (end && *end == '\0' && end != v) {
                    r = FpState{static_cast<uint32_t>(mx) & 0xffff, static_cast<uint16_t>(cw), false, "custom"};
                    r.ps4 = r.mxcsr == kPs4.mxcsr && r.fcw == kPs4.fcw;
                } else {
                    std::fprintf(stderr, "[lbppc] LBP3_FP_STATE=%s not understood (ps4, lbppc or <mxcsr>[:<fcw>] in hex): ps4\n", v);
                    r = kPs4;
                }
            }
        }
        if (!r.ps4 || Mode())
            std::fprintf(stderr, "[lbppc] game threads' floating-point state: MXCSR %#x, x87 %#x (%s)\n", r.mxcsr, r.fcw, r.name);
        return r;
    }();
    return s;
}

}  // namespace core::lbppc
