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
//   flip_buffers, flip_dwords, first_to_last_enq_us, pre_submits, pre_buffers, pre_dwords,
//   pre_submit_us     the flip submit's command buffers and their hand-overs; earlier submits
//
// First-frame columns (milestone 5, perf/intro_stalls.md), over the WHOLE frame: from the first
// command buffer's hand-over to the flip (all of the frame's buffers, its Finish and readback),
// from the video core's Lbp3Perf counters (shadPS4 video_core/lbp3_perf.h). _n counts, _us
// microseconds of CPU time on the command processor unless named gpu, _kb kilobytes:
//   ff_record_us      first hand-over to the flip packet: recording every buffer of the frame
//   ff_draws, ff_dispatches      draws and dispatches that reached resource binding
//   ff_bufsync_*      buffer cache uploads of CPU-written guest memory (memcpy to staging + copy)
//   ff_staging_*      staging pool requests (may wait for the GPU); _new: new blocks, their KB
//   ff_resident_*     device memory allocated to back sparse arenas
//   ff_arena_*        new sparse arenas
//   ff_img_new*       new images created and registered by the texture cache
//   ff_img_alloc_*    vmaCreateImage; ff_buf_alloc_*: vmaCreateBuffer
//   ff_vma_mem_*      vkAllocateMemory calls made by VMA (count, KB)
//   ff_img_refresh_*  image uploads (RefreshImage: buffer sync + detile + copy; nests the above)
//   ff_detile_*       detile dispatches recorded; ff_expand_*: image expansions
//   ff_res_bind_us    resource binding per draw/dispatch (nests the caches above);
//   ff_desc_bind_us   descriptor writes/pushes
//   ff_gpu_copy_*, ff_gpu_upload_*, ff_gpu_detile_*
//                     GPU time of buffer copies, buffer-to-image uploads and detile dispatches
//                     (timestamp pairs; LBP3_FIRST_FRAME only, else 0); ff_gpu_overflow: pairs
//                     skipped because the timestamp pool was full
//   ff_img_free_*     image destruction (vmaDestroyImage); ff_vma_free_*: VMA's vkFreeMemory calls
//   ff_gpu_imgcopy_*  GPU time of image-to-image copies (ExpandImage's copy of the old image)
//   ff_window_us      the counters' window: first hand-over to the later of Flipped and Released
//                     (the counters above cover this window, not only ff_record_us)
//   ff_bufs_at_flip   buffers handed over when the flip interrupt came; less than flip_buffers
//                     means the flip was signalled before the frame's last buffer was recorded
//   ff_memcpy_*       guest memory -> staging copies (CopySparseMemory) inside buffer sync and
//                     image refresh
//   ff_page_faults    the whole process's page faults over the window (GetProcessMemoryInfo)
//   ff_memcpy_zero_kb of the memcpy bytes, 4 KB chunks that were all zero (LBP3_FIRST_FRAME only)
// LBP3_FIRST_FRAME=1 turns all of this on (and the GPU timestamps); LBP3_FRAME_SPLIT=1 alone
// gives the CPU counters. A frame whose ff_record_us exceeds 50 ms also prints a one-line
// "[first-frame] ..." breakdown to stderr.
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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

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
    PerfCounters atFirst{};     // counters at the frame's first hand-over (whole-frame columns)
    PerfCounters atRel{};       // counters at Released (the whole-frame window's end, if later)
    std::atomic<uint32_t> enqCount{0}, bufsAtFlip{0};  // hand-overs so far / at the flip interrupt
    std::atomic<int> parts{0};  // 1: released, 2: flipped
    // The flip submit's buffers, the first buffer's hand-over, and the frame's earlier
    // sceGnmSubmitCommandBuffers calls (no flip) since the previous flip submit.
    std::atomic<int64_t> firstEnq{0}, preUs{0};
    std::atomic<uint32_t> flipBufs{0}, flipDw{0}, preCalls{0}, preBufs{0}, preDw{0};
};

Slot g_ring[kRing];
std::mutex g_fileMutex;
FILE* g_csv = nullptr;
std::atomic<uint64_t> g_nextFrame{1};
thread_local uint64_t t_frame = 0;
int64_t g_lastReleasedUs = -1;  // main thread only
int64_t g_lastFlippedUs = -1;   // command-processor thread only
// Main thread only: sceGnmSubmitCommandBuffers calls since the last flip submit.
int64_t g_preUs = 0;
uint32_t g_preCalls = 0, g_preBufs = 0, g_preDw = 0;

// The first-frame columns (whole frame: first hand-over to the flip), from the video core's
// Lbp3Perf counters (PerfCounters::ff*, Lbp3Perf::Kind order). N = count, US = microseconds,
// KB = bytes / 1024.
enum Field { N, US, KB };
struct FfColumn {
    const char* name;
    int kind;
    Field field;
};
constexpr FfColumn kFfColumns[] = {
    {"ff_draws", 14, N},          {"ff_dispatches", 15, N},
    {"ff_bufsync_n", 0, N},       {"ff_bufsync_us", 0, US},       {"ff_bufsync_kb", 0, KB},
    {"ff_staging_n", 1, N},       {"ff_staging_us", 1, US},       {"ff_staging_new", 2, N},
    {"ff_staging_new_kb", 2, KB}, {"ff_resident_n", 3, N},        {"ff_resident_us", 3, US},
    {"ff_resident_kb", 3, KB},    {"ff_arena_new", 4, N},         {"ff_arena_us", 4, US},
    {"ff_img_new", 5, N},         {"ff_img_new_us", 5, US},       {"ff_img_alloc_n", 6, N},
    {"ff_img_alloc_us", 6, US},   {"ff_img_alloc_kb", 6, KB},     {"ff_buf_alloc_n", 7, N},
    {"ff_buf_alloc_us", 7, US},   {"ff_buf_alloc_kb", 7, KB},     {"ff_vma_mem_n", 8, N},
    {"ff_vma_mem_kb", 8, KB},     {"ff_img_refresh_n", 9, N},     {"ff_img_refresh_us", 9, US},
    {"ff_img_refresh_kb", 9, KB}, {"ff_detile_n", 10, N},         {"ff_detile_us", 10, US},
    {"ff_expand_n", 11, N},       {"ff_expand_us", 11, US},       {"ff_res_bind_us", 12, US},
    {"ff_desc_bind_us", 13, US},  {"ff_gpu_copy_n", 16, N},       {"ff_gpu_copy_us", 16, US},
    {"ff_gpu_upload_n", 17, N},   {"ff_gpu_upload_us", 17, US},   {"ff_gpu_detile_n", 18, N},
    {"ff_gpu_detile_us", 18, US}, {"ff_gpu_overflow", 19, N},
    {"ff_img_free_n", 20, N},     {"ff_img_free_us", 20, US},     {"ff_vma_free_n", 21, N},
    {"ff_vma_free_kb", 21, KB},   {"ff_gpu_imgcopy_n", 22, N},    {"ff_gpu_imgcopy_us", 22, US},
    {"ff_memcpy_n", 23, N},       {"ff_memcpy_us", 23, US},       {"ff_memcpy_kb", 23, KB},
    {"ff_memcpy_zero_kb", 24, KB},
};

// The counters plus the process's page-fault count (a snapshot for the whole-frame columns).
PerfCounters Snap() {
    PerfCounters c = ReadPerfCounters();
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) c.pageFaults = pmc.PageFaultCount;
    return c;
}

uint64_t FfValue(const PerfCounters& a, const PerfCounters& b, int kind, Field f) {
    switch (f) {
    case N: return b.ffN[kind] - a.ffN[kind];
    case US: return b.ffUs[kind] - a.ffUs[kind];
    default: return (b.ffBytes[kind] - a.ffBytes[kind]) / 1024;
    }
}

// Frames whose recording (first hand-over to the flip packet) is longer get a [first-frame] line.
constexpr int64_t kFirstFrameLogUs = 50000;

bool Open() {
    static const bool on = [] {
        if (!std::getenv("LBP3_FRAME_SPLIT") && !std::getenv("LBP3_FIRST_FRAME")) return false;
        g_csv = std::fopen("analysis\\frame_split.csv", "w");
        if (!g_csv) {
            std::fprintf(stderr, "[split] can't write analysis\\frame_split.csv\n");
            return false;
        }
        std::fprintf(g_csv,
                     "frame,submit_ms,main_cpu_us,waitidle_us,waitidle_rel,prep_us,record_us,gfx_blocked_us,"
                     "gfx_blocked_waits,finish_us,copy_us,slot_us,present_us,release_after_flip_us,submit_rel,"
                     "period_us,pipelines,pipeline_us,shaders,shader_us,flip_buffers,flip_dwords,"
                     "first_to_last_enq_us,pre_submits,pre_buffers,pre_dwords,pre_submit_us,ff_record_us");
        for (const FfColumn& c : kFfColumns) std::fprintf(g_csv, ",%s", c.name);
        std::fprintf(g_csv, ",ff_window_us,ff_bufs_at_flip,ff_page_faults\n");
        std::fprintf(stderr, "[split] writing analysis\\frame_split.csv (LBP3_FRAME_SPLIT / LBP3_FIRST_FRAME)\n");
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
    const int64_t firstToLast =
        s.firstEnq.load() && s.t[Enqueued].load() ? s.t[Enqueued].load() - s.firstEnq.load() : -1;
    std::fprintf(g_csv, "%llu,%.3f,%lld,%lld,%d,%lld,%lld,%lld,%u,%lld,%lld,%lld,%lld,%lld,%d,%lld,%u,%llu,%u,%llu,"
                        "%u,%u,%lld,%u,%u,%u,%lld",
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
                 static_cast<unsigned long long>(s.at1.shaderUs - s.at0.shaderUs), s.flipBufs.load(), s.flipDw.load(),
                 static_cast<long long>(firstToLast), s.preCalls.load(), s.preBufs.load(), s.preDw.load(),
                 static_cast<long long>(s.preUs.load()));
    // The whole frame's recording and the video core's counters over it (first hand-over to flip).
    const bool haveFirst = s.firstEnq.load() != 0;
    const int64_t ffRecordUs = haveFirst && s.t[FlipIrq].load() ? s.t[FlipIrq].load() - s.firstEnq.load() : -1;
    const PerfCounters& from = haveFirst ? s.atFirst : s.at0;
    // The window ends at the later of Flipped and Released: with the flip signalled before the
    // frame's last buffer (ff_bufs_at_flip < flip_buffers), recording goes on after the flip.
    const int64_t tFlipped = s.t[Flipped].load(), tReleased = s.t[Released].load();
    const bool relLater = tReleased > tFlipped;
    const PerfCounters& to = relLater ? s.atRel : s.at1;
    const int64_t windowUs = haveFirst ? (relLater ? tReleased : tFlipped) - s.firstEnq.load() : -1;
    const uint32_t bufsAtFlip = s.bufsAtFlip.load(), flipBufs = s.flipBufs.load();
    std::fprintf(g_csv, ",%lld", static_cast<long long>(ffRecordUs));
    for (const FfColumn& c : kFfColumns)
        std::fprintf(g_csv, ",%llu", static_cast<unsigned long long>(FfValue(from, to, c.kind, c.field)));
    const unsigned long long pageFaults = to.pageFaults - from.pageFaults;
    std::fprintf(g_csv, ",%lld,%u,%llu\n", static_cast<long long>(windowUs), bufsAtFlip, pageFaults);
    const bool earlyFlip = bufsAtFlip < flipBufs;
    if (ffRecordUs > kFirstFrameLogUs || (earlyFlip && windowUs > kFirstFrameLogUs)) {
        auto v = [&](int kind, Field f) { return static_cast<unsigned long long>(FfValue(from, to, kind, f)); };
        std::fprintf(stderr,
                     "[first-frame] frame %llu: recording %.1f ms over %u buffers, GPU %.1f ms, %llu draws, "
                     "%llu dispatches | buffer sync %llu x %.1f ms %llu KB | staging %.1f ms, %llu new | resident "
                     "%llu x %.1f ms %llu KB | arenas %llu | new images %llu x %.1f ms | image alloc %llu x %.1f ms "
                     "%llu KB | buffer alloc %llu x %.1f ms | VMA memory %llu x %llu KB | refresh %llu x %.1f ms "
                     "%llu KB | detile %llu x %.1f ms | expand %llu x %.1f ms | bind %.1f ms, descriptors %.1f ms | "
                     "GPU copy %llu x %.1f ms, upload %llu x %.1f ms, detile %llu x %.1f ms (overflow %llu) | "
                     "image free %llu x %.1f ms | VMA free %llu x %llu KB | GPU image copy %llu x %.1f ms | "
                     "window %.1f ms, flip after %u of %u buffers | memcpy %llu x %.1f ms %llu KB "
                     "(%llu KB all-zero) | page faults %llu\n",
                     static_cast<unsigned long long>(frame), ffRecordUs / 1000.0, s.flipBufs.load(),
                     Span(s, FinishBegin, FinishEnd) / 1000.0, v(14, N), v(15, N), v(0, N), v(0, US) / 1000.0,
                     v(0, KB), v(1, US) / 1000.0, v(2, N), v(3, N), v(3, US) / 1000.0, v(3, KB), v(4, N), v(5, N),
                     v(5, US) / 1000.0, v(6, N), v(6, US) / 1000.0, v(6, KB), v(7, N), v(7, US) / 1000.0, v(8, N),
                     v(8, KB), v(9, N), v(9, US) / 1000.0, v(9, KB), v(10, N), v(10, US) / 1000.0, v(11, N),
                     v(11, US) / 1000.0, v(12, US) / 1000.0, v(13, US) / 1000.0, v(16, N), v(16, US) / 1000.0,
                     v(17, N), v(17, US) / 1000.0, v(18, N), v(18, US) / 1000.0, v(19, N), v(20, N),
                     v(20, US) / 1000.0, v(21, N), v(21, KB), v(22, N), v(22, US) / 1000.0, windowUs / 1000.0,
                     bufsAtFlip, flipBufs, v(23, N), v(23, US) / 1000.0, v(23, KB), v(24, KB), pageFaults);
    }
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
    s.at0 = s.at1 = s.atFirst = s.atRel = PerfCounters{};
    s.enqCount = s.bufsAtFlip = 0;
    s.parts = 0;
    s.firstEnq = 0;
    s.flipBufs = s.flipDw = 0;
    s.preUs = g_preUs;
    s.preCalls = g_preCalls;
    s.preBufs = g_preBufs;
    s.preDw = g_preDw;
    g_preUs = 0;
    g_preCalls = g_preBufs = g_preDw = 0;
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
    if (p == Enqueued && !s->firstEnq.load()) {
        s->firstEnq = now;
        s->atFirst = Snap();
    }
    if (p == Enqueued) s->enqCount.fetch_add(1);
    if (p == FlipIrq) s->bufsAtFlip = s->enqCount.load();
    s->t[p] = now;
    switch (p) {
    case Enqueued: s->at0 = ReadPerfCounters(); break;
    case Flipped:
        s->at1 = Snap();
        s->periodUs = g_lastFlippedUs >= 0 ? now - g_lastFlippedUs : -1;
        g_lastFlippedUs = now;
        Complete(frame, *s, 2);
        break;
    case Released:
        s->atRel = Snap();
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

void NoteSubmit(int64_t us, uint32_t buffers, uint32_t dwords) {
    if (!Open()) return;
    g_preUs += us;
    ++g_preCalls;
    g_preBufs += buffers;
    g_preDw += dwords;
}

void SetFlipBuffers(uint32_t buffers, uint32_t dwords) {
    if (!Open()) return;
    if (Slot* s = SlotFor(t_frame)) {
        s->flipBufs = buffers;
        s->flipDw = dwords;
    }
}

void SetResults(int waitIdleResult, int submitResult) {
    if (!Open()) return;
    if (Slot* s = SlotFor(t_frame)) {
        s->waitIdleResult = waitIdleResult;
        s->submitResult = submitResult;
    }
}

}  // namespace gpu::split
