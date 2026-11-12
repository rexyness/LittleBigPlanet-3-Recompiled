// Frame generation (opt-in, LBP3_FRAMEGEN=1; perf/fg_present.md in the planning repo).
//
// The game keeps flipping 30 times a second, exactly as without this: its flip slots, the
// flip-anchored clock and its flip events don't change. Only the window changes: each real
// frame N is shown half a game period later than before, and an in-between frame, made from
// the real frames N-1 and N, is shown where N would have been. So the window shows 60 frames a
// second at the cost of about half a game period (~17 ms) of display latency, plus the delay
// that leaves room to make the in-between frame (LBP3_FRAMEGEN_DELAY_MS, default one refresh
// + 4 ms).
//
// The command-processor thread hands each real frame over (SubmitReal) in place of
// video::PresentPixels: a buffer swap and a notify, no copy. A worker thread decides whether an
// in-between frame makes sense (not for the first frame, after a late frame or a load, when the
// game stepped its simulation 0 or 2 times, at a scene cut, or when the size changed), makes it
// with the chosen generator, and queues both frames for the presenter with display times
// (video::PresentTimed). The presenter shows, at each refresh, the latest frame due by then.
//
// Generators work on host copies only (BGRA8 rows): the game's images are never written.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace video::framegen {

// LBP3_FRAMEGEN=1 (read once). Everything below is inert when it's off.
bool Enabled();

// A real frame as the command-processor thread hands it over.
struct RealInfo {
    int64_t slotNs = 0;    // steady-clock time of the frame's flip slot (when it would be shown without this)
    int64_t periodNs = 0;  // the game's flip period (33.3 ms at the game's 30 Hz)
    int simSteps = -1;     // simulation steps the game ran for this frame (-1 = unknown)
};
// Takes the frame (pixels comes back holding a spare buffer to refill) and returns at once.
void SubmitReal(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, const RealInfo& info);

// The status line, printed with the [video] line every 30 flips (nothing when off).
void PrintStatus();

// ---- Generators -------------------------------------------------------------------------------

struct HostFrame {
    const uint8_t* pixels = nullptr;  // BGRA8, rows of width * 4 bytes
    uint32_t width = 0, height = 0;
};
struct GenInputs {
    HostFrame prev, cur;  // the real frames N-1 and N
    float t = 0.5f;       // where between them (0 = prev, 1 = cur)
    uint32_t flags = 0;
    // Later (lane A): the game's motion vectors, depth and HUD-less colour for this frame.
    const void* motionVectors = nullptr;
    const void* depth = nullptr;
    const void* hudless = nullptr;
};
class Generator {
public:
    virtual ~Generator() = default;
    virtual const char* Name() const = 0;
    // Writes the in-between frame into out (resized to width * height * 4). False = drop it.
    virtual bool Generate(const GenInputs& in, std::vector<uint8_t>& out) = 0;
};
// "blend" (50/50 average of N-1 and N, the default) or "repeat" (N-1 again: the window then
// shows 30 distinct frames a second at the 60 Hz cadence, for timing checks). nullptr = unknown.
std::unique_ptr<Generator> MakeGenerator(const char* name);

// ---- Decisions (pure, for the worker and the offline test) -----------------------------------

enum class Drop : int { None, First, Size, Rate, Late, Steps, Cut, Generator, GenLate, kCount };
const char* DropName(Drop d);
struct DecideIn {
    bool havePrev = false;
    bool sameSize = false;
    int64_t sinceLastSlotNs = 0;  // this frame's slot minus the previous real frame's
    int64_t periodNs = 0;
    int simSteps = -1;
    double cutDiff = 0;       // MeanAbsDiff of the two frames
    double cutThreshold = 0;  // a cut when cutDiff is above it
};
Drop Decide(const DecideIn& in);
// Mean absolute difference of B, G and R (0..255) over a sparse grid (every 8th pixel of every
// 8th row): cheap scene-cut detection.
double MeanAbsDiff(const HostFrame& a, const HostFrame& b);
// out = rounded average of a and b, byte by byte (same size).
void Blend(const uint8_t* a, const uint8_t* b, uint8_t* out, size_t bytes);
// The display delay of the in-between frame after the real frame's slot: overrideMs when >= 0,
// else one refresh at refreshHz (60 Hz when unknown) + 4 ms.
int64_t DelayNs(double refreshHz, double overrideMs);

// Counters since the last call (PrintStatus and the offline test).
struct Stats {
    uint64_t submitted = 0, generated = 0, genLateShown = 0;
    uint64_t dropped[static_cast<int>(Drop::kCount)] = {};
    double genMsSum = 0, genMsMax = 0, diffSum = 0, diffMax = 0;
    uint64_t diffCount = 0;
};
Stats TakeStats();

}  // namespace video::framegen
