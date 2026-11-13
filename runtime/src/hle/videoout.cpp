// libSceVideoOut + the submit/flip half of libSceGnmDriver, as scaffolding.
//
// There's no window or GPU emulation yet. Command buffers are ignored, but
// every submit reports the GPU events the game registered (end-of-pipe etc.)
// as if the work finished instantly, and flips are paced to 60 Hz and reported
// through flip events. That keeps the game's frame loop running at a real speed.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

#include "core/runner.h"
#include "hle/abi.h"
#include "hle/kernel_errors.h"
#include "hle/kernel_sync.h"
#include "hle/pm4.h"
#include "hle/register.h"
#include "gpu/frame_split.h"
#include "gpu/renderer.h"
#include "gpu/fg_live.h"
#include "video/framegen.h"
#include "video/framegen_warp.h"
#include "video/presenter.h"

namespace VideoCore {
extern std::atomic<uint32_t> g_lbp3ImageCreates, g_lbp3ImageExpands, g_lbp3ImageReadbacks;  // texture_cache.cpp
extern std::atomic<uint32_t> g_lbp3Refreshes, g_lbp3RefreshesCpu, g_lbp3FillClears, g_lbp3PreSyncs,
    g_lbp3PreSyncSkips;
extern std::atomic<uint64_t> g_lbp3RefreshBytes, g_lbp3FillClearBytes, g_lbp3PreSyncBytes;
}
namespace Core {
extern std::atomic<uint64_t> g_lbp3ProtectFast, g_lbp3ProtectWalk;  // gpu/video_core_glue.cpp
}

namespace hle::kernel {
void NoteFlip(int64_t steadyNs);  // kernel_time.cpp: the main thread's frame-steady clock
void NoteMainThread();
}  // namespace hle::kernel

namespace hle::videoout {

using Clock = std::chrono::steady_clock;

constexpr int kHandle = 1;  // single main display port
// Flip events use an internal ident and pack the flip argument above bit 16.
constexpr uint64_t kInternalFlipEventId = 0x6;
constexpr auto kFrameTime = std::chrono::nanoseconds(1'000'000'000 / 60);
constexpr uint64_t kFlipsPerStatus = 30;  // a status line every ~0.5 s

std::mutex g_mutex;
std::vector<void*> g_flipQueues;
struct GpuEvent { void* equeue; uint64_t id; };
std::vector<GpuEvent> g_gpuEvents;
Clock::time_point g_nextFlip = Clock::now();
std::atomic<int> g_flipRate{0};  // sceVideoOutSetFlipRate: 0 = 60 Hz, 1 = 30 Hz, 2 = 20 Hz
uint64_t g_flipCount = 0;
gpu::DisplayBufferDesc g_displayBuffers[16];

// With the GPU model active, a flip completes when the command processor
// reaches the frame's flip packet (GfxFlip interrupt), not at submit time.
// splitFrame: gpu/frame_split.h. simSteps: the frame's simulation steps, read only with frame
// generation on (video/framegen.h), else -1.
struct PendingFlip { int buffer; int64_t arg; uint64_t splitFrame = 0; int simSteps = -1; double camJump = -1; int64_t cpuStartNs = 0; };
std::mutex g_pendingMutex;
std::deque<PendingFlip> g_pendingFlips;
constexpr uint32_t kIrqGfxFlip = 0x08;

// Waits for this flip's slot. Flips land on vblanks, every (rate + 1) of the
// console's 60 Hz ones. Use the window's real vsync when the rates divide evenly,
// so every frame stays on screen equally long; otherwise a 60 Hz timer.
// Returns true when the flip was paced by vblanks, false when by the timer grid.
bool WaitFlipSlotInner() {
    const int vblanksPerFlip = g_flipRate.load() + 1;
    int64_t slotNs = 0;
    if (video::WaitForFlipSlot(60.0 / vblanksPerFlip, &slotNs)) {
        g_nextFlip = Clock::now();
    } else {
        // Flips stay on a fixed grid of vblanks: the next slot follows from the last slot,
        // never from when a late frame arrived. Restarting the grid at each late frame
        // made the average period drift above the game's fixed step, so every few seconds
        // it ran two steps in one frame and none in the next (a camera snap). The game
        // also paces itself to ~33.3 ms, so frames often arrive just after their slot:
        // up to half a vblank late they flip at once (the grid keeps its beat); later
        // than that they wait for the next vblank on the grid.
        const auto period = kFrameTime * vblanksPerFlip;
        const auto now = Clock::now();
        if (g_nextFlip + period * 4 < now) g_nextFlip = now;  // far behind (a load): resync
        while (g_nextFlip + kFrameTime / 2 < now) g_nextFlip += kFrameTime;
        // Hit the slot precisely, as vsync would: Windows sleeps overshoot by up to a
        // millisecond or so, and the game's fixed 30 Hz simulation then sees uneven frame
        // times and runs 0 or 2 steps in a frame (the camera snap). Sleep most of the way,
        // then spin for the last 2 ms.
        std::this_thread::sleep_until(g_nextFlip - std::chrono::milliseconds(2));
        while (Clock::now() < g_nextFlip) std::this_thread::yield();
        // The slot this frame belongs to, even if it arrived a little late: the main
        // thread's steady clock anchors on the grid, not on when the flip happened.
        kernel::NoteFlip(std::chrono::duration_cast<std::chrono::nanoseconds>(g_nextFlip.time_since_epoch()).count());
        g_nextFlip += period;
        return false;
    }
    // The vblank's ideal time, so the game's frame clock sees exact frame deltas.
    kernel::NoteFlip(slotNs ? slotNs
                            : std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
    return true;
}

// Pacing statistics for the [video] status line: time spent waiting for the slot (the
// frame's headroom under the cap) and how many flips fell back to the timer.
double g_paceWaitMs = 0, g_paceWorkMs = 0;
int g_paceTimerFlips = 0;

// ---- LBP3_LOW_LATENCY: a Reflex-style limiter (the idea of LBPPC's low-latency limiter,
// emurphy04 aad6a91, reimplemented; none of its code). Measured on our port (perf/best_gfx.md):
// LBP3's frame thread doesn't run a frame ahead or block anywhere. It takes the flip event without
// waiting, paces itself on the frame clock, starts each frame at the flip slot and submits it in
// time; the GPU then finishes 10-15 ms before the next slot and waits there (the [video] "slot
// wait"). Holding the frame thread back a few ms when it takes the flip event moves its pad read
// (and the rest of its frame) closer to the slot, so a press shows sooner. The delay counts from
// the slot and stays inside the frame clock's dead zone (kernel_time.cpp, 8 ms): the frame's clock
// reads still return the slot's time, so the simulation's steps don't change.
//   LBP3_LOW_LATENCY=1: on. =0: measure only (the [lowlat] line). Unset: nothing.
// Delay = the 10th percentile over 60 frames of (slot wait + the delay that frame had) minus a
// 3 ms margin, at most 6 ms, moving at most 0.5 ms a frame; a frame that nearly misses its slot
// (under 0.5 ms of slot wait) halves it.
int LowLatencyMode() {
    static const int mode = [] {
        const char* v = std::getenv("LBP3_LOW_LATENCY");
        return v && v[0] == '1' ? 1 : v && v[0] == '0' ? 0 : -1;
    }();
    return mode;
}
constexpr int64_t kLlMaxDelayNs = 6'000'000;
std::atomic<int64_t> g_llDelayNs{0};    // set on the GPU thread after each slot, used by the frame thread
std::atomic<int64_t> g_llAppliedNs{0};  // what the frame thread took after the last flip event
int64_t g_llCpuStartNs = 0;             // frame thread: when its current frame began (after any delay)
std::atomic<int64_t> g_llLatUs{0}, g_llLatCount{0};    // frame start to slot, summed at the flips
std::atomic<int64_t> g_llTakeUs{0}, g_llTakeCount{0};  // flip event taken how long after its slot
std::atomic<int64_t> g_llSlotWaitUs{0}, g_llLate{0};   // GPU-side slot wait and near misses
// The frame thread's blocking waits since its last submit, by kind: 0 a flip event, 1 other equeue
// events, 2 event flags, 3 semaphores, 4 condition variables, 5 usleep (kernel_*.cpp report them).
constexpr int kLlKinds = 6;
int64_t g_llWaitNs[kLlKinds] = {}, g_llWaitN[kLlKinds] = {};

int64_t SteadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

void LowLatencyNoteWait(int kind, int64_t ns) {
    if (kind < 0 || kind >= kLlKinds) return;
    g_llWaitNs[kind] += ns;
    ++g_llWaitN[kind];
}

// GPU thread, after each slot wait: the slack (the slot wait plus the delay that frame had) sets
// the next delay.
void LowLatencyAfterSlotWait(double waitMs) {
    if (LowLatencyMode() < 0) return;
    static double ring[60];
    static int n = 0;
    ring[n++ % 60] = waitMs + g_llAppliedNs.load() / 1e6;
    g_llSlotWaitUs += static_cast<int64_t>(waitMs * 1000);
    if (LowLatencyMode() != 1) return;
    double cur = g_llDelayNs.load() / 1e6;
    const int cnt = std::min(n, 60);
    double target = 0;
    if (cnt >= 30) {
        double tmp[60];
        std::copy(ring, ring + cnt, tmp);
        std::nth_element(tmp, tmp + cnt / 10, tmp + cnt);
        target = std::clamp(tmp[cnt / 10] - 3.0, 0.0, kLlMaxDelayNs / 1e6);
    }
    if (waitMs < 0.5) {
        cur *= 0.5;
        ++g_llLate;
    } else {
        cur += std::clamp(target - cur, -0.5, 0.5);
    }
    g_llDelayNs.store(static_cast<int64_t>(cur * 1e6));
}

}  // namespace hle::videoout
namespace hle::kernel {
int64_t LastFlipNs();  // kernel_time.cpp: the last flip's slot (steady clock)
}  // namespace hle::kernel
namespace hle::videoout {

// The frame thread, when it takes a flip event (kernel_sync.cpp, outside the queue's lock).
void LowLatencyAtFlipEvent() {
    if (LowLatencyMode() < 0) return;
    const int64_t slot = kernel::LastFlipNs();
    const int64_t now = SteadyNowNs();
    if (slot && now >= slot && now - slot < 100'000'000) g_llTakeUs += (now - slot) / 1000, ++g_llTakeCount;
    int64_t applied = 0;
    if (LowLatencyMode() == 1 && slot) {
        const int64_t delay = std::min(g_llDelayNs.load(), kLlMaxDelayNs);
        const int64_t target = slot + delay;
        if (delay > 0 && target > now) {
            const auto until = Clock::time_point(std::chrono::nanoseconds(target));
            if (target - now > 1'500'000) std::this_thread::sleep_until(until - std::chrono::milliseconds(1));
            while (Clock::now() < until) std::this_thread::yield();
            applied = SteadyNowNs() - now;
        }
    }
    g_llAppliedNs.store(applied);
    g_llCpuStartNs = SteadyNowNs();
}

// The frame thread, right after each submit: the status line (every 30 submits).
void LowLatencyAfterSubmit(double preSubmitWaitMs) {
    if (LowLatencyMode() < 0) return;
    static int frames = 0;
    static double sumPre = 0, sumWait[kLlKinds] = {}, sumN[kLlKinds] = {};
    sumPre += preSubmitWaitMs;
    for (int k = 0; k < kLlKinds; ++k) sumWait[k] += g_llWaitNs[k] / 1e6, sumN[k] += g_llWaitN[k], g_llWaitNs[k] = 0, g_llWaitN[k] = 0;
    if (++frames < 30) return;
    const int64_t latN = g_llLatCount.exchange(0), latUs = g_llLatUs.exchange(0);
    const int64_t takeN = g_llTakeCount.exchange(0), takeUs = g_llTakeUs.exchange(0);
    std::fprintf(stderr,
                 "[lowlat] %s: delay %.2f ms, slot wait %.2f ms avg, frame start to slot %.1f ms avg (%lld), flip "
                 "event taken %.2f ms after its slot (%lld), near misses %lld; frame-thread waits ms/frame: pre-submit "
                 "%.2f, flip %.2f (%.1f/frame), equeue %.2f, evflag %.2f, sema %.2f, cond %.2f, usleep %.2f\n",
                 LowLatencyMode() == 1 ? "on" : "measure", g_llDelayNs.load() / 1e6, g_llSlotWaitUs.exchange(0) / 1000.0 / frames,
                 latN ? latUs / 1000.0 / latN : 0.0, static_cast<long long>(latN), takeN ? takeUs / 1000.0 / takeN : 0.0,
                 static_cast<long long>(takeN), static_cast<long long>(g_llLate.exchange(0)), sumPre / frames,
                 sumWait[0] / frames, sumN[0] / frames, sumWait[1] / frames, sumWait[2] / frames, sumWait[3] / frames,
                 sumWait[4] / frames, sumWait[5] / frames);
    frames = 0, sumPre = 0;
    for (int k = 0; k < kLlKinds; ++k) sumWait[k] = 0, sumN[k] = 0;
}

// GPU thread, at a flip: how long ago that frame's CPU work began (cpuStartNs from its submit).
void LowLatencyAtSlot(int64_t cpuStartNs, int64_t slotNs) {
    if (LowLatencyMode() < 0 || !cpuStartNs || slotNs <= cpuStartNs || slotNs - cpuStartNs > 200'000'000) return;
    g_llLatUs += (slotNs - cpuStartNs) / 1000;
    ++g_llLatCount;
}

void WaitFlipSlot() {
    static Clock::time_point lastEnd = Clock::now();
    const auto start = Clock::now();
    gpu::split::Mark(gpu::split::SlotBegin);
    const bool vsynced = WaitFlipSlotInner();
    gpu::split::Mark(gpu::split::SlotEnd);
    const auto end = Clock::now();
    const double waitMs = std::chrono::duration<double, std::milli>(end - start).count();
    const double workMs = std::chrono::duration<double, std::milli>(start - lastEnd).count();
    lastEnd = end;
    g_paceWaitMs += waitMs;
    g_paceWorkMs += workMs;
    g_paceTimerFlips += !vsynced;
    LowLatencyAfterSlotWait(waitMs);
    // LBP3_PACE_LOG=1: one line per flip in analysis\pace.csv.
    static FILE* csv = [] {
        FILE* f = std::getenv("LBP3_PACE_LOG") ? std::fopen("analysis\\pace.csv", "w") : nullptr;
        if (f) std::fprintf(f, "flip,end_ms,work_ms,wait_ms,path,vblanks\n");
        return f;
    }();
    if (csv) {
        static uint64_t n = 0;
        std::fprintf(csv, "%llu,%.3f,%.3f,%.3f,%c,%llu\n", static_cast<unsigned long long>(n++),
                     std::chrono::duration<double, std::milli>(end.time_since_epoch()).count(), workMs, waitMs,
                     vsynced ? 'v' : 't', static_cast<unsigned long long>(video::VBlankCount()));
        if (n % 300 == 0) std::fflush(csv);
    }
}

// Milestone 5 measurement, printed after each [video] line (always on; counters only):
// first-sight compiles in the window and their time, the graphics queue's blocked time per
// frame and the number of blocked waits, and early releases per call site (released on a
// known address + after the 20 ms grace): the main thread's wait before a submit, the async
// compute submit, the graphics submit.
void PrintPerfLine() {
    static gpu::PerfCounters last{};
    static uint32_t lastRel[3][2] = {};
    const gpu::PerfCounters now = gpu::ReadPerfCounters();
    uint32_t rel[3][2];
    gpu::ReadReleaseCounts(rel);
    std::fprintf(stderr,
                 "[perf] compiled %u pipelines (%.0f ms), %u shaders (%.0f ms); gfx blocked %.1f ms/frame in %u waits; "
                 "early releases: wait-idle %u+%u, compute %u+%u, submit %u+%u (known+grace); dropped %u draws\n",
                 now.pipelineCompiles - last.pipelineCompiles, (now.pipelineUs - last.pipelineUs) / 1000.0,
                 now.shaderCompiles - last.shaderCompiles, (now.shaderUs - last.shaderUs) / 1000.0,
                 static_cast<double>(now.gfxBlockedNs - last.gfxBlockedNs) / 1e6 / kFlipsPerStatus,
                 now.gfxBlockedWaits - last.gfxBlockedWaits, rel[0][0] - lastRel[0][0], rel[0][1] - lastRel[0][1],
                 rel[1][0] - lastRel[1][0], rel[1][1] - lastRel[1][1], rel[2][0] - lastRel[2][0],
                 rel[2][1] - lastRel[2][1], now.droppedDraws - last.droppedDraws);
    last = now;
    for (int s = 0; s < 3; ++s)
        for (int k = 0; k < 2; ++k) lastRel[s][k] = rel[s][k];
    // Image refreshes and the game's render-target fills, per frame (perf/reuploads.md).
    constexpr double kMB = 1024.0 * 1024.0 * kFlipsPerStatus;
    std::fprintf(stderr,
                 "[fills] per frame: %.1f refreshes (%.1f MB, %.1f CPU-dirty), %.1f cleared (%.1f MB), "
                 "%.1f pre-syncs (%.1f MB), %.1f skipped\n",
                 VideoCore::g_lbp3Refreshes.exchange(0) / double(kFlipsPerStatus),
                 VideoCore::g_lbp3RefreshBytes.exchange(0) / kMB,
                 VideoCore::g_lbp3RefreshesCpu.exchange(0) / double(kFlipsPerStatus),
                 VideoCore::g_lbp3FillClears.exchange(0) / double(kFlipsPerStatus),
                 VideoCore::g_lbp3FillClearBytes.exchange(0) / kMB,
                 VideoCore::g_lbp3PreSyncs.exchange(0) / double(kFlipsPerStatus),
                 VideoCore::g_lbp3PreSyncBytes.exchange(0) / kMB,
                 VideoCore::g_lbp3PreSyncSkips.exchange(0) / double(kFlipsPerStatus));
    // Write-tracking Protect calls in the window: one VirtualProtect, or the region walk
    // (LBP3_PROTECT=old, or the single call failed). perf/gfx_wait.md fix 1.
    std::fprintf(stderr, "[protect] %llu fast, %llu walked\n",
                 static_cast<unsigned long long>(Core::g_lbp3ProtectFast.exchange(0)),
                 static_cast<unsigned long long>(Core::g_lbp3ProtectWalk.exchange(0)));
    video::framegen::PrintStatus();  // nothing unless LBP3_FRAMEGEN=1
    gpu::split::Flush();
}

// Reports a completed flip to the game.
void SignalFlip(int64_t flipArg) {
    std::lock_guard lk(g_mutex);
    // Frame-time statistics over each status interval: average, worst, frames over 40 ms.
    static Clock::time_point lastFlip = Clock::now();
    static double sumMs = 0, worstMs = 0;
    static int slow = 0;
    const auto now = Clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - lastFlip).count();
    lastFlip = now;
    sumMs += ms;
    worstMs = std::max(worstMs, ms);
    slow += ms > 40.0;
    if (++g_flipCount % kFlipsPerStatus == 0) {
        std::fprintf(stderr,
                     "[video] %llu frames flipped (avg %.1f ms, worst %.1f ms, %d over 40 ms; %u new images, "
                     "%u expanded, %u read back; %u early releases, %u wait addresses learned; work %.1f ms, slot wait %.1f ms, "
                     "%d on timer)\n",
                     static_cast<unsigned long long>(g_flipCount), sumMs / kFlipsPerStatus, worstMs, slow,
                     VideoCore::g_lbp3ImageCreates.exchange(0), VideoCore::g_lbp3ImageExpands.exchange(0),
                     VideoCore::g_lbp3ImageReadbacks.exchange(0),
                     gpu::TakeEarlyReleases(), gpu::LearnedWaits(), g_paceWorkMs / kFlipsPerStatus,
                     g_paceWaitMs / kFlipsPerStatus, g_paceTimerFlips);
        PrintPerfLine();
        sumMs = worstMs = 0;
        slow = 0;
        g_paceWaitMs = g_paceWorkMs = 0;
        g_paceTimerFlips = 0;
    }
    for (void* eq : g_flipQueues)
        kernel::TriggerEvent(eq, kInternalFlipEventId, kernel::kEvfiltVideoOut,
                             static_cast<int64_t>(kInternalFlipEventId | (static_cast<uint64_t>(flipArg) << 16)));
}

// Pretends the GPU finished everything submitted so far.
void SignalGpuEvents() {
    std::lock_guard lk(g_mutex);
    for (const auto& e : g_gpuEvents)
        kernel::TriggerEvent(e.equeue, e.id, kernel::kEvfiltGraphicsCore, static_cast<int64_t>(e.id));
}

int PS4_SYSV_ABI sceVideoOutOpen(int32_t userId, int32_t busType, int32_t index, const void* param) {
    (void)userId;
    (void)busType;
    (void)index;
    (void)param;
    video::StartPresenter();
    static const bool gpuReady = [] {
        try {
            if (!gpu::Init()) return false;
            // Frame generation's warp runs on the GPU (perf/fg_gpu.md) unless
            // LBP3_FRAMEGEN_WARP_GPU=0 (then the CPU version, video/framegen_warp.cpp).
            if (video::framegen::Enabled()) {
                const char* mode = std::getenv("LBP3_FRAMEGEN_MODE");
                const char* onGpu = std::getenv("LBP3_FRAMEGEN_WARP_GPU");
                if (mode && std::strcmp(mode, "warp") == 0 && !(onGpu && std::strcmp(onGpu, "0") == 0))
                    gpu::fglive::Configure(video::framegen::WarpParamsFromEnv());
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[gpu] renderer unavailable (%s); using the scaffolding path\n", e.what());
            return false;
        }
        // GPU interrupts fire the game's sceGnmAddEqEvent registrations with that id.
        gpu::SetInterruptHandler([](uint32_t irq) {
            if (irq == kIrqGfxFlip) {
                PendingFlip f{-1, 0, 0};
                {
                    std::lock_guard pl(g_pendingMutex);
                    if (!g_pendingFlips.empty()) {
                        f = g_pendingFlips.front();
                        g_pendingFlips.pop_front();
                    }
                }
                if (f.buffer >= 0) {
                    gpu::split::SetCpFrame(f.splitFrame);
                    gpu::split::Mark(gpu::split::FlipIrq);
                    video::PresentBuffer(f.buffer);
                    // Pull the rendered image back for our window (on the GPU thread).
                    static std::vector<uint8_t> pixels;  // recycled through the presenter
                    uint32_t w = 0, h = 0;
                    if (f.buffer < 16) gpu::fg::OnFlip(g_displayBuffers[f.buffer].address, static_cast<uint32_t>(f.buffer));
                    gpu::MaybeDumpRenderTargets();
                    const bool got =
                        f.buffer < 16 && gpu::ReadbackDisplayBuffer(g_displayBuffers[f.buffer], pixels, w, h);
                    // Show the frame on its slot, not whenever the GPU finished it:
                    // uneven hand-over times make a moving camera judder.
                    if (got) gpu::fg::OnFinal(pixels, w, h, g_displayBuffers[f.buffer].pixelFormat);
                    // GPU frame generation: submit this frame's in-between work now (its inputs
                    // were copied during the frame and by the readback); the worker waits for it.
                    video::framegen::GpuTicket gpuTicket;
                    if (gpu::fglive::Active()) gpuTicket = gpu::fglive::Kick();
                    WaitFlipSlot();
                    LowLatencyAtSlot(f.cpuStartNs, kernel::LastFlipNs());
                    gpu::fg::OnShown();
                    if (got) {
                        if (video::framegen::Enabled()) {
                            // Frame generation: the same hand-over point and cost (a buffer swap);
                            // the window shows the frame later, with an in-between frame first.
                            // The slot, the game's clock and the flip event below don't change.
                            video::framegen::RealInfo info;
                            info.slotNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              Clock::now().time_since_epoch())
                                              .count();
                            info.periodNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                kFrameTime * (g_flipRate.load() + 1))
                                                .count();
                            info.simSteps = f.simSteps;
                            info.camJump = f.camJump;
                            info.gpu = gpuTicket;
                            video::framegen::SubmitReal(pixels, w, h, info);
                        } else {
                            video::PresentPixels(pixels, w, h);
                        }
                    }
                    SignalFlip(f.arg);
                    gpu::split::Mark(gpu::split::Flipped);
                    gpu::split::SetCpFrame(0);
                }
                return;
            }
            std::lock_guard lk(g_mutex);
            static int irqLogged = 0;
            if (irqLogged < 8) {
                ++irqLogged;
                std::fprintf(stderr, "[gnm] interrupt 0x%x (%zu event registrations)\n", irq, g_gpuEvents.size());
            }
            for (const auto& e : g_gpuEvents)
                if (e.id == irq) kernel::TriggerEvent(e.equeue, e.id, kernel::kEvfiltGraphicsCore, static_cast<int64_t>(e.id));
        });
        return true;
    }();
    (void)gpuReady;
    return kHandle;
}

struct BufferAttribute {
    uint32_t pixelFormat, tilingMode;
    int32_t aspectRatio;
    uint32_t width, height, pitchInPixel, option, reserved0;
    uint64_t reserved1;
};

void PS4_SYSV_ABI sceVideoOutSetBufferAttribute(BufferAttribute* attr, uint32_t pixelFormat, uint32_t tilingMode,
                                                uint32_t aspectRatio, uint32_t width, uint32_t height,
                                                uint32_t pitchInPixel) {
    if (!attr) return;
    *attr = {pixelFormat, tilingMode, static_cast<int32_t>(aspectRatio), width, height, pitchInPixel, 0, 0, 0};
}

int PS4_SYSV_ABI sceVideoOutRegisterBuffers(int32_t handle, int32_t start, void* const* addresses, int32_t count,
                                            const BufferAttribute* attr) {
    if (handle != kHandle) return static_cast<int>(0x80290001);
    if (!addresses || !attr) return static_cast<int>(0x80290002);  // INVALID_ADDRESS
    std::fprintf(stderr, "[video] %d display buffers %ux%u (pitch %u, format 0x%x, tiling %u)\n", count, attr->width,
                 attr->height, attr->pitchInPixel, attr->pixelFormat, attr->tilingMode);
    for (int i = 0; i < count; ++i)
    {
        video::SetDisplayBuffer(start + i, {addresses[i], attr->width, attr->height, attr->pitchInPixel,
                                            attr->pixelFormat, attr->tilingMode});
        if (start + i < 16)
            g_displayBuffers[start + i] = {reinterpret_cast<uint64_t>(addresses[i]), attr->width, attr->height,
                                           attr->pitchInPixel, attr->pixelFormat, attr->tilingMode};
        gpu::fg::SetDisplayBuffer(start + i, reinterpret_cast<uint64_t>(addresses[i]));
        gpu::fglive::SetDisplayBuffer(start + i, reinterpret_cast<uint64_t>(addresses[i]));
    }
    return start;  // the buffer set index
}

int PS4_SYSV_ABI sceVideoOutSetFlipRate(int32_t handle, int32_t rate) {
    if (handle != kHandle) return static_cast<int>(0x80290001);  // INVALID_HANDLE
    if (rate < 0 || rate > 2) return static_cast<int>(0x80290006);  // INVALID_VALUE
    std::fprintf(stderr, "[video] flip rate %d Hz\n", 60 / (rate + 1));
    g_flipRate = rate;
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceVideoOutAddFlipEvent(void* equeue, int32_t handle, void* udata) {
    if (handle != kHandle) return static_cast<int>(0x80290001);  // INVALID_HANDLE
    kernel::AddEvent(equeue, kInternalFlipEventId, kernel::kEvfiltVideoOut, udata);
    std::lock_guard lk(g_mutex);
    g_flipQueues.push_back(equeue);
    return kernel::SCE_OK;
}

}  // namespace hle::videoout

namespace hle::gnm {
namespace {

// One 64-bit "flip pending" label per display buffer (sceVideoOutGetBufferLabelAddress).
uint64_t g_bufferLabels[16];

// The game leaves a 64-dword "prepare flip" NOP at the end of the last command
// buffer of a flipped frame; the Gnm driver rewrites it into a label write
// and/or an end-of-pipe event. Ported from shadPS4's PatchFlipRequest.
// The kPatchedFlip NOP always lands 59 dwords before the buffer's end, and the command
// processor flips only on a marker at exactly that position of a top-level buffer
// (shadPS4 liverpool.cpp, PatchedFlip case; perf/early_flip.md): keep the two in step.
bool PatchFlipRequest(uint32_t* cmd, uint32_t sizeDw, uint32_t bufferIndex) {
    constexpr uint32_t kPrepareFlipHeader = 0xC03E1000;  // type-3 NOP, 63 body dwords
    enum Payload : uint32_t {
        kPrepareFlip = 0x68750777u, kPrepareFlipLabel = 0x68750778u,
        kPrepareFlipInterrupt = 0x68750780u, kPrepareFlipInterruptLabel = 0x68750781u,
        kPatchedFlip = 0x68750776u,
    };
    constexpr uint32_t kOpNop = 0x10, kOpWriteData = 0x37, kOpEventWriteEop = 0x47;
    auto type3 = [](uint32_t op, uint32_t countMinusOne) { return (3u << 30) | (countMinusOne << 16) | (op << 8); };

    if (sizeDw < 64 || bufferIndex >= std::size(g_bufferLabels)) return false;
    cmd += sizeDw - 64;
    if (cmd[0] != kPrepareFlipHeader) {
        static int warned = 0;
        if (warned++ < 3) std::fprintf(stderr, "[gnm] no prepareFlip packet at the end of the frame\n");
        return false;
    }
    uint32_t b[7];
    std::memcpy(b, cmd, sizeof(b));

    // WRITE_DATA: mark the display buffer as pending (cleared when the flip completes).
    const uint64_t labelAddr = reinterpret_cast<uint64_t>(&g_bufferLabels[bufferIndex]);
    cmd[0] = type3(kOpWriteData, 3);
    cmd[1] = 0x500u;
    cmd[2] = static_cast<uint32_t>(labelAddr);
    cmd[3] = static_cast<uint32_t>(labelAddr >> 32);
    cmd[4] = 1;

    uint32_t* nop = cmd + 5;
    nop[1] = kPatchedFlip;
    if (b[1] == kPrepareFlip) {
        nop[0] = type3(kOpNop, 0x39);
    } else if (b[1] == kPrepareFlipLabel) {
        nop[0] = type3(kOpNop, 0x34);
        uint32_t* w = cmd + 0x3B;  // WRITE_DATA to the game's label
        w[0] = type3(kOpWriteData, 3);
        w[1] = 0x500u;
        w[2] = b[2] & 0xFFFFFFFCu;
        w[3] = b[3];
        w[4] = b[4];
    } else if (b[1] == kPrepareFlipInterruptLabel || b[1] == kPrepareFlipInterrupt) {
        nop[0] = type3(kOpNop, 0x33);
        uint32_t* e = cmd + 0x3A;  // EVENT_WRITE_EOP with interrupt
        e[0] = type3(kOpEventWriteEop, 4);
        e[1] = (b[5] & 0x3F) + 0x500u + (b[6] & 0x3F) * 0x1000;
        if (b[1] == kPrepareFlipInterruptLabel) {
            e[2] = b[2] & 0xFFFFFFFCu;
            e[3] = (b[3] & 0xFFFFu) | 0x22000000u;
            e[4] = b[4];
        } else {
            e[2] = 0;
            e[3] = 0x1000000u;
            e[4] = 0;
        }
        e[5] = 0;
    }
    return true;
}

// LBP3_FLIP_TRACE=1 (perf/early_flip.md's proof run): one line per flip submit with each
// command buffer's game address and size and where PatchFlipRequest put the flip marker (the
// NOP 59 dwords before the last buffer's end). The command processor parses snapshot copies,
// so its "[gnm] ignored a stale flip marker" line gives only a dword offset and a buffer size;
// this line maps that back to game memory and to the frame that left the marker. Off normally.
void TraceFlipSubmit(uint32_t count, void* const* dcbs, const uint32_t* dcbSizes, uint64_t splitFrame) {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_FLIP_TRACE");
        return v && std::strcmp(v, "1") == 0;
    }();
    if (!on || count == 0) return;
    static uint64_t submits = 0;
    ++submits;
    std::string line;
    char part[96];
    std::snprintf(part, sizeof(part), "[flip] submit %llu (split frame %llu): %u buffer(s):",
                  static_cast<unsigned long long>(submits), static_cast<unsigned long long>(splitFrame), count);
    line = part;
    for (uint32_t i = 0; i < count; ++i) {
        std::snprintf(part, sizeof(part), " %u@0x%llx/%u", i,
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(dcbs[i])), dcbSizes[i] / 4);
        line += part;
    }
    const uint32_t lastDw = dcbSizes[count - 1] / 4;
    if (lastDw >= 64) {
        const uint64_t nop = reinterpret_cast<uintptr_t>(dcbs[count - 1]) + uint64_t(lastDw - 59) * 4;
        std::snprintf(part, sizeof(part), "; patched NOP at 0x%llx (dword %u of buffer %u)",
                      static_cast<unsigned long long>(nop), lastDw - 59, count - 1);
        line += part;
    }
    std::fprintf(stderr, "%s\n", line.c_str());
}

// Runs the memory-writing packets of each buffer, then raises the GPU
// interrupts the game listens for.
// Returns how the last graphics submit's wait returned (gpu::WaitResult), or kWaitNone.
int Process(uint32_t count, void* const* dcbs, const uint32_t* dcbSizes, void* const* ccbs, const uint32_t* ccbSizes) {
    if (gpu::Active()) {
        // shadPS4's command processor executes the buffers and raises the interrupts.
        for (uint32_t i = 0; i < count; ++i) {
            const auto* d = static_cast<const uint32_t*>(dcbs[i]);
            const long bad = pm4::FindOverrun(d, dcbSizes[i] / 4);
            static int reported = 0;
            if (bad >= 0 && reported++ < 3) {
                std::fprintf(stderr, "[gnm] dcb of %u dwords: packet at %ld overruns; tail:", dcbSizes[i] / 4, bad);
                for (uint32_t k = static_cast<uint32_t>(bad); k < dcbSizes[i] / 4; ++k) std::fprintf(stderr, " %08x", d[k]);
                std::fprintf(stderr, "\n");
            }
        }
        int result = gpu::kWaitNone;
        for (uint32_t i = 0; i < count; ++i)
            result = gpu::SubmitGfx(static_cast<const uint32_t*>(dcbs[i]), dcbSizes[i],
                                    ccbs ? static_cast<const uint32_t*>(ccbs[i]) : nullptr, ccbSizes ? ccbSizes[i] : 0);
        return result;
    }
    bool irq = false;
    static std::atomic<int> submits{0};
    const int n = ++submits;
    for (uint32_t i = 0; i < count; ++i) {
        pm4::Result d{}, c{};
        if (dcbs && dcbs[i]) d = pm4::Execute(static_cast<const uint32_t*>(dcbs[i]), dcbSizes[i]);
        if (ccbs && ccbs[i]) c = pm4::Execute(static_cast<const uint32_t*>(ccbs[i]), ccbSizes[i]);
        irq |= d.interrupt || c.interrupt;
        if (n <= 60 || n % 300 == 0)
            std::fprintf(stderr, "[gnm] submit #%d buf %u: dcb %u bytes, %zu draws, %zu dispatches, target 0x%llx%s\n", n,
                         i, dcbSizes ? dcbSizes[i] : 0, d.draws, d.dispatches,
                         static_cast<unsigned long long>(d.lastColorTarget), d.interrupt ? ", irq" : "");
    }
    if (irq) videoout::SignalGpuEvents();
    return gpu::kWaitNone;
}

}  // namespace

int PS4_SYSV_ABI sceGnmAddEqEvent(void* equeue, uint64_t id, void* udata) {
    if (!equeue) return kernel::SceErr(kernel::kEBADF);
    kernel::AddEvent(equeue, id, kernel::kEvfiltGraphicsCore, udata);
    std::fprintf(stderr, "[gnm] AddEqEvent id 0x%llx\n", static_cast<unsigned long long>(id));
    std::lock_guard lk(videoout::g_mutex);
    videoout::g_gpuEvents.push_back({equeue, id});
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceGnmDeleteEqEvent(void* equeue, uint64_t id) {
    std::lock_guard lk(videoout::g_mutex);
    std::erase_if(videoout::g_gpuEvents, [&](const auto& e) { return e.equeue == equeue && e.id == id; });
    return kernel::SCE_OK;
}

// The event type lives in the event's data field.
int PS4_SYSV_ABI sceGnmGetEqEventType(const int64_t* event) { return static_cast<int>(event[2]); }

int PS4_SYSV_ABI sceGnmAreSubmitsAllowed() { return 1; }

int PS4_SYSV_ABI sceGnmSubmitCommandBuffers(uint32_t count, void* const* dcbs, const uint32_t* dcbSizes,
                                            void* const* ccbs, const uint32_t* ccbSizes) {
    static int logged = 0;
    if (logged++ < 5)
        std::fprintf(stderr, "[gnm] submit %u command buffer(s), first %u bytes\n", count, count ? dcbSizes[0] : 0);
    const auto t0 = std::chrono::steady_clock::now();
    Process(count, dcbs, dcbSizes, ccbs, ccbSizes);
    if (gpu::split::Enabled()) {
        uint32_t dw = 0;
        for (uint32_t i = 0; i < count; ++i) dw += dcbSizes[i] / 4;
        gpu::split::NoteSubmit(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count(),
                               count, dw);
    }
    return kernel::SCE_OK;
}

// LBP3_TRACE_SIM=1: how many simulation steps (GetSimFrame__, eboot+0x7c79e0) and game
// frames (GetGameFrame__, 0x7c79f0) each flipped frame contains, 60 flips per line. A
// smooth camera needs exactly one sim step per flip.
void TraceSimSteps() {
    static const bool enabled = std::getenv("LBP3_TRACE_SIM") != nullptr;
    if (!enabled) return;
    uint8_t* base = core::EbootBase();
    if (!base) return;
    using Counter = int(PS4_SYSV_ABI*)();
    const int sim = reinterpret_cast<Counter>(base + 0x7c79e0)();
    const int game = reinterpret_cast<Counter>(base + 0x7c79f0)();
    static int lastSim = sim, lastGame = game;
    static std::string simSteps, gameSteps;
    using SimClock = std::chrono::steady_clock;
    static SimClock::time_point lastTime = SimClock::now();
    static std::string gaps;
    const auto now = SimClock::now();
    const int ds = sim - lastSim, dg = game - lastGame;
    simSteps += ds >= 0 && ds < 10 ? static_cast<char>('0' + ds) : '?';
    gameSteps += dg >= 0 && dg < 10 ? static_cast<char>('0' + dg) : '?';
    const int ms = static_cast<int>(std::chrono::duration<double, std::milli>(now - lastTime).count() + 0.5);
    gaps += std::to_string(ms) + " ";
    lastSim = sim;
    lastGame = game;
    lastTime = now;
    if (simSteps.size() >= 60) {
        std::fprintf(stderr, "[sim] steps per flip: %s | game frames: %s\n[sim] ms between submits: %s\n",
                     simSteps.c_str(), gameSteps.c_str(), gaps.c_str());
        simSteps.clear();
        gameSteps.clear();
        gaps.clear();
    }
}

// Frame generation only (video/framegen.h): the simulation steps since the previous flip
// submit (GetSimFrame__, read as TraceSimSteps does), so no in-between frame is made across a
// 0- or 2-step frame. -1 when frame generation is off (nothing is read) or for the first flip.
// Frame generation only: how far the camera moved since the last flip submit (the largest change
// of the main view's translation, DAT_013a2a60's camera matrix at +0x20; perf/fg_inputs.md: 5,753
// at a camera cut, at most ~250 in play). -1 when off or unknown.
double FrameGenCamJump() {
    if (!video::framegen::Enabled()) return -1;
    uint8_t* base = core::EbootBase();
    if (!base) return -1;
    uint64_t view = 0;
    std::memcpy(&view, base + 0x13a2a60, sizeof(view));
    static bool haveLast = false;
    static float last[3] = {};
    if (view <= 0x100000 || view >= (uint64_t{1} << 47)) {
        haveLast = false;
        return -1;
    }
    float t[3];
    std::memcpy(t, reinterpret_cast<const uint8_t*>(view) + 0x20 + 12 * 4, sizeof t);
    double jump = -1;
    if (haveLast) {
        jump = 0;
        for (int i = 0; i < 3; ++i) jump = std::max(jump, double(std::fabs(t[i] - last[i])));
        if (!(jump == jump)) jump = -1;  // NaN
    }
    std::memcpy(last, t, sizeof t);
    haveLast = true;
    return jump;
}

int FrameGenSimSteps() {
    if (!video::framegen::Enabled()) return -1;
    uint8_t* base = core::EbootBase();
    if (!base) return -1;
    using Counter = int(PS4_SYSV_ABI*)();
    const int sim = reinterpret_cast<Counter>(base + 0x7c79e0)();
    static bool haveLast = false;
    static int last = 0;
    const int steps = haveLast ? sim - last : -1;
    haveLast = true;
    last = sim;
    return steps;
}

int PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffers(uint32_t count, void* const* dcbs, const uint32_t* dcbSizes,
                                                   void* const* ccbs, const uint32_t* ccbSizes, int32_t videoOutHandle,
                                                   uint32_t displayBufferIndex, uint32_t flipMode, int64_t flipArg) {
    (void)videoOutHandle;
    static const bool noted = (kernel::NoteMainThread(), true);  // the game's frame thread
    (void)noted;
    TraceSimSteps();
    const int simSteps = FrameGenSimSteps();
    const double camJump = FrameGenCamJump();
    if (gpu::fg::Enabled()) {  // LBP3_FG_DUMP: counters and the main view (camera now +0x20, before +0x160)
        if (uint8_t* base = core::EbootBase()) {
            using Counter = int(PS4_SYSV_ABI*)();
            const int sim = reinterpret_cast<Counter>(base + 0x7c79e0)();
            const int game = reinterpret_cast<Counter>(base + 0x7c79f0)();
            uint64_t view = 0;
            std::memcpy(&view, base + 0x13a2a60, sizeof(view));  // DAT_013a2a60 (ClearMotionBlur__)
            gpu::fg::NoteSubmit(sim, game, displayBufferIndex, view > 0x100000 && view < (uint64_t{1} << 47) ? reinterpret_cast<const uint8_t*>(view) : nullptr);
        }
    }
    if (count && gpu::Active()) {
        const uint64_t splitFrame = gpu::split::BeginFrame();  // LBP3_FRAME_SPLIT measurement
        if (splitFrame) {
            uint32_t dw = 0;
            for (uint32_t i = 0; i < count; ++i) dw += dcbSizes[i] / 4;
            gpu::split::SetFlipBuffers(count, dw);
        }
        // Like the real driver: the previous frame must be done before this one is patched.
        const auto idleBegin = std::chrono::steady_clock::now();
        const int idleResult = gpu::WaitIdle();
        const double idleMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - idleBegin).count();
        gpu::split::Mark(gpu::split::WaitIdleEnd);
        if (PatchFlipRequest(static_cast<uint32_t*>(dcbs[count - 1]), dcbSizes[count - 1] / 4, displayBufferIndex)) {
            {
                std::lock_guard pl(videoout::g_pendingMutex);
                videoout::g_pendingFlips.push_back({static_cast<int>(displayBufferIndex), flipArg, splitFrame, simSteps, camJump,
                                                    videoout::g_llCpuStartNs});
            }
            TraceFlipSubmit(count, dcbs, dcbSizes, splitFrame);
            const int submitResult = Process(count, dcbs, dcbSizes, ccbs, ccbSizes);
            gpu::split::SetResults(idleResult, submitResult);
            gpu::split::Mark(gpu::split::Released);
            videoout::LowLatencyAfterSubmit(idleMs);  // LBP3_LOW_LATENCY (off unless set)
            return kernel::SCE_OK;  // flip completes on the GfxFlip interrupt
        }
    }
    (void)displayBufferIndex;
    (void)flipMode;
    static int logged = 0;
    if (logged++ < 5)
        std::fprintf(stderr, "[gnm] submit+flip %u command buffer(s), first %u bytes, buffer %u\n", count,
                     count ? dcbSizes[0] : 0, displayBufferIndex);
    Process(count, dcbs, dcbSizes, ccbs, ccbSizes);
    videoout::WaitFlipSlot();
    video::PresentBuffer(static_cast<int>(displayBufferIndex));
    videoout::SignalFlip(flipArg);
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceGnmSubmitDone() { return kernel::SCE_OK; }

// Async compute: the game writes commands into a ring and rings the doorbell.
int PS4_SYSV_ABI sceGnmMapComputeQueue(uint32_t pipeId, uint32_t queueId, void* ringBase, uint32_t ringSizeDw,
                                       uint32_t* readPtr) {
    return gpu::MapComputeQueue(pipeId, queueId, reinterpret_cast<uint64_t>(ringBase), ringSizeDw, readPtr);
}

int PS4_SYSV_ABI sceGnmMapComputeQueueWithPriority(uint32_t pipeId, uint32_t queueId, void* ringBase,
                                                   uint32_t ringSizeDw, uint32_t* readPtr, uint32_t priority) {
    (void)priority;
    return sceGnmMapComputeQueue(pipeId, queueId, ringBase, ringSizeDw, readPtr);
}

int PS4_SYSV_ABI sceGnmUnmapComputeQueue(uint32_t vqid) {
    gpu::UnmapComputeQueue(vqid);
    return kernel::SCE_OK;
}

void PS4_SYSV_ABI sceGnmDingDong(uint32_t vqid, uint32_t nextOffsDw) {
    if (gpu::Active()) gpu::DingDong(vqid, nextOffsDw);
}

void PS4_SYSV_ABI sceGnmDingDongForWorkload(uint32_t vqid, uint32_t nextOffsDw, uint64_t workloadId) {
    (void)workloadId;
    sceGnmDingDong(vqid, nextOffsDw);
}

}  // namespace hle::gnm

namespace hle {

void RegisterVideoOut(core::Linker& linker) {
    using namespace videoout;
    using namespace gnm;
    HLE_REG(linker, "libSceVideoOut", sceVideoOutOpen);
    HLE_REG(linker, "libSceVideoOut", sceVideoOutAddFlipEvent);
    HLE_REG(linker, "libSceVideoOut", sceVideoOutSetFlipRate);
    HLE_REG(linker, "libSceVideoOut", sceVideoOutSetBufferAttribute);
    HLE_REG(linker, "libSceVideoOut", sceVideoOutRegisterBuffers);
    HLE_REG(linker, "libSceGnmDriver", sceGnmSubmitAndFlipCommandBuffers);
    HLE_REG(linker, "libSceGnmDriver", sceGnmSubmitDone);
    HLE_REG(linker, "libSceGnmDriver", sceGnmMapComputeQueue);
    HLE_REG(linker, "libSceGnmDriver", sceGnmMapComputeQueueWithPriority);
    HLE_REG(linker, "libSceGnmDriver", sceGnmUnmapComputeQueue);
    HLE_REG(linker, "libSceGnmDriver", sceGnmDingDong);
    HLE_REG(linker, "libSceGnmDriver", sceGnmDingDongForWorkload);
    HLE_REG(linker, "libSceGnmDriver", sceGnmSubmitCommandBuffers);
    HLE_REG(linker, "libSceGnmDriver", sceGnmAddEqEvent);
    HLE_REG(linker, "libSceGnmDriver", sceGnmDeleteEqEvent);
    HLE_REG(linker, "libSceGnmDriver", sceGnmGetEqEventType);
    HLE_REG(linker, "libSceGnmDriver", sceGnmAreSubmitsAllowed);
}

}  // namespace hle
