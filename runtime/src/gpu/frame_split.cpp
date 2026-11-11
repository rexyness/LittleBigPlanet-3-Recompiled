// Milestone 5 measurement (see frame_split.h). analysis\frame_split.csv, one row per frame,
// times in microseconds (steady clock):
//
//   frame             the frame's id (counts sceGnmSubmitAndFlipCommandBuffers calls)
//   submit_ms         when the main thread entered the submit (steady clock, ms)
//   main_cpu_us       main thread's own time for this frame: from being released after the
//                     previous frame's submit to entering this one (game code and job waits)
//   waitidle_us       main, in gpu::WaitIdle before patching this frame (previous frame's queues)
//   waitidle_rel      how that wait returned: 0 idle, 1 released on a known address, 2 after
//                     the 20 ms grace, -1 not measured
//   prep_us           main, from WaitIdle's return to handing the buffer over (patch, copy)
//   record_us         command processor, from the hand-over to this frame's flip packet:
//                     parsing and Vulkan recording of every draw, first-sight compiles, and
//                     any time the graphics queue sat blocked (gfx_blocked_us)
//   gfx_blocked_us    graphics queue blocked on WAIT_REG_MEM/MEM_SEMAPHORE during the frame
//   gfx_blocked_waits how many such waits
//   finish_us         the frame's Vulkan submit and the wait for the GPU to run it
//   copy_us           the display image's copy back to the CPU (invalidate + memcpy)
//   slot_us           waiting for the flip's slot (vblank, or the 60 Hz grid at 165 Hz)
//   present_us        from the slot to the flip being reported (hand-over to the window)
//   release_after_flip_us  main's release minus the flip report: positive, the main thread
//                     waited for the rest of the buffer after the flip; negative, the game was
//                     released before the flip (the graphics queue blocked: an early release)
//   submit_rel        how the submit's wait returned (as waitidle_rel; -1 with LBP3_GPU_ASYNC)
//   period_us         flip to flip (this frame's on-screen period as the game sees it)
//   pipelines, pipeline_us, shaders, shader_us
//                     first-sight compiles between the hand-over and the flip, and their time
//
// A missing point (e.g. no readback) shows as -1. The file is flushed every 60 rows and when
// the status line prints; kill the game and the last few rows may be lost.
#include "gpu/frame_split.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "gpu/renderer.h"

namespace gpu::split {
namespace {

using Clock = std::chrono::steady_clock;

int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

constexpr size_t kRing = 32;

struct Slot {
    std::atomic<uint64_t> frame{0};
    std::array<std::atomic<int64_t>, kPoints> t{};
    std::atomic<int> waitIdleResult{-1}, submitResult{-1};
    std::atomic<int64_t> mainCpuUs{-1}, periodUs{-1};
    PerfCounters at0{}, at1{};  // counters at Enqueued (main) and at Flipped (command processor)
    std::atomic<int> parts{0};  // 1: released, 2: flipped
};

Slot g_ring[kRing];
std::mutex g_fileMutex;
FILE* g_csv = nullptr;
std::atomic<uint64_t> g_nextFrame{1};
thread_local uint64_t t_frame = 0;
int64_t g_lastReleasedUs = -1;  // main thread only
int64_t g_lastFlippedUs = -1;   // command-processor thread only

bool Open() {
    static const bool on = [] {
        if (!std::getenv("LBP3_FRAME_SPLIT")) return false;
        g_csv = std::fopen("analysis\\frame_split.csv", "w");
        if (!g_csv) {
            std::fprintf(stderr, "[split] can't write analysis\\frame_split.csv\n");
            return false;
        }
        std::fprintf(g_csv,
                     "frame,submit_ms,main_cpu_us,waitidle_us,waitidle_rel,prep_us,record_us,gfx_blocked_us,"
                     "gfx_blocked_waits,finish_us,copy_us,slot_us,present_us,release_after_flip_us,submit_rel,"
                     "period_us,pipelines,pipeline_us,shaders,shader_us\n");
        std::fprintf(stderr, "[split] writing analysis\\frame_split.csv (LBP3_FRAME_SPLIT)\n");
        return true;
    }();
    return on;
}

Slot* SlotFor(uint64_t frame) {
    if (!frame) return nullptr;
    Slot& s = g_ring[frame % kRing];
    return s.frame.load(std::memory_order_acquire) == frame ? &s : nullptr;
}

int64_t Span(const Slot& s, Point a, Point b) {
    const int64_t ta = s.t[a].load(), tb = s.t[b].load();
    return ta && tb ? tb - ta : -1;
}

void WriteRow(uint64_t frame, Slot& s) {
    // The counter deltas need the hand-over's snapshot (at0); without it they would be totals.
    if (!s.t[Enqueued].load()) s.at0 = s.at1;
    const int64_t blockedUs =
        s.t[Enqueued].load() ? static_cast<int64_t>((s.at1.gfxBlockedNs - s.at0.gfxBlockedNs) / 1000) : -1;
    std::lock_guard lk(g_fileMutex);
    std::fprintf(g_csv, "%llu,%.3f,%lld,%lld,%d,%lld,%lld,%lld,%u,%lld,%lld,%lld,%lld,%lld,%d,%lld,%u,%llu,%u,%llu\n",
                 static_cast<unsigned long long>(frame), s.t[SubmitEnter].load() / 1000.0,
                 static_cast<long long>(s.mainCpuUs.load()), static_cast<long long>(Span(s, SubmitEnter, WaitIdleEnd)),
                 s.waitIdleResult.load(), static_cast<long long>(Span(s, WaitIdleEnd, Enqueued)),
                 static_cast<long long>(Span(s, Enqueued, FlipIrq)), static_cast<long long>(blockedUs),
                 s.at1.gfxBlockedWaits - s.at0.gfxBlockedWaits, static_cast<long long>(Span(s, FinishBegin, FinishEnd)),
                 static_cast<long long>(Span(s, FinishEnd, CopyEnd)), static_cast<long long>(Span(s, SlotBegin, SlotEnd)),
                 static_cast<long long>(Span(s, SlotEnd, Flipped)), static_cast<long long>(Span(s, Flipped, Released)),
                 s.submitResult.load(), static_cast<long long>(s.periodUs.load()),
                 s.at1.pipelineCompiles - s.at0.pipelineCompiles,
                 static_cast<unsigned long long>(s.at1.pipelineUs - s.at0.pipelineUs),
                 s.at1.shaderCompiles - s.at0.shaderCompiles,
                 static_cast<unsigned long long>(s.at1.shaderUs - s.at0.shaderUs));
    static unsigned rows = 0;
    if (++rows % 60 == 0) std::fflush(g_csv);
}

void Complete(uint64_t frame, Slot& s, int part) {
    if ((s.parts.fetch_or(part) | part) == 3) WriteRow(frame, s);
}

}  // namespace

bool Enabled() { return Open(); }

uint64_t BeginFrame() {
    if (!Open()) return 0;
    const uint64_t frame = g_nextFrame++;
    Slot& s = g_ring[frame % kRing];
    s.frame.store(0, std::memory_order_release);  // invalid while it is reset
    for (auto& t : s.t) t = 0;
    s.waitIdleResult = s.submitResult = -1;
    s.periodUs = -1;
    s.at0 = s.at1 = PerfCounters{};
    s.parts = 0;
    const int64_t now = NowUs();
    s.t[SubmitEnter] = now;
    s.mainCpuUs = g_lastReleasedUs >= 0 ? now - g_lastReleasedUs : -1;
    s.frame.store(frame, std::memory_order_release);
    t_frame = frame;
    return frame;
}

void SetCpFrame(uint64_t frame) {
    if (Open()) t_frame = frame;
}

void Mark(Point p) {
    if (!Open()) return;
    const uint64_t frame = t_frame;
    Slot* s = SlotFor(frame);
    if (!s) return;
    const int64_t now = NowUs();
    s->t[p] = now;
    switch (p) {
    case Enqueued: s->at0 = ReadPerfCounters(); break;
    case Flipped:
        s->at1 = ReadPerfCounters();
        s->periodUs = g_lastFlippedUs >= 0 ? now - g_lastFlippedUs : -1;
        g_lastFlippedUs = now;
        Complete(frame, *s, 2);
        break;
    case Released:
        g_lastReleasedUs = now;
        Complete(frame, *s, 1);
        break;
    default: break;
    }
}

void Flush() {
    if (!Open()) return;
    std::lock_guard lk(g_fileMutex);
    std::fflush(g_csv);
}

void SetResults(int waitIdleResult, int submitResult) {
    if (!Open()) return;
    if (Slot* s = SlotFor(t_frame)) {
        s->waitIdleResult = waitIdleResult;
        s->submitResult = submitResult;
    }
}

}  // namespace gpu::split
