// The LBPPC co-op mode (docs/research/lbppc-coop.md): LBP3_LBPPC_COOP=1, the launcher's "Play with
// LBPPC players". Online co-op runs every player's world in lockstep, so to play with players on
// LBPPC's client (github.com/emurphy04/LBPPC) this game must simulate exactly as theirs does, and the
// peer link must speak their protocol (net/p2p_lbppc.h). Off, nothing here changes anything.
#pragma once
#include <cstdint>

namespace core::lbppc {

// LBP3_LBPPC_COOP=1 (read once).
bool Mode();

// The floating-point control state game threads and fibers run in: LBP3_FP_STATE=
//   ps4    MXCSR 0x9fc0 (exceptions masked, flush-to-zero, denormals-are-zero), x87 0x037f: the
//          console's, and this port's default (hle/kernel_pthread.cpp; it fixed the Bunkum ending);
//   lbppc  what LBPPC's game threads run in, measured on a running LBPPC 0.1.5: MXCSR 0x1f80 (no
//          flush-to-zero or denormals-are-zero), x87 0x037f. It sets none itself (LBPPC cb4cbe7
//          src/runtime_thread.c host_start; its fibers are Win32 fibers that ignore the game's "set
//          FPU registers" request, src/runtime_extra.c fiber_init);
//   <mxcsr>[:<fcw>], hex: anything else, for experiments.
// Default: ps4, or lbppc in the LBPPC co-op mode.
struct FpState {
    uint32_t mxcsr;
    uint16_t fcw;
    bool ps4;            // the console's state (fibers get it when the game asks)
    const char* name;
};
FpState GuestFp();

}  // namespace core::lbppc
