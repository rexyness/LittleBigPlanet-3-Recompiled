// The game window. Shows whichever display buffer the game flipped last.
//
// Display buffers are what the GPU renders into. Until draw commands are
// executed they hold nothing meaningful, and they're usually tiled, so the
// picture is garbage for now; the window, buffer registration and pacing are
// what's real here.
#pragma once
#include <cstdint>
#include <vector>

namespace video {

struct DisplayBuffer {
    const void* address = nullptr;
    uint32_t width = 0, height = 0, pitchPixels = 0;
    uint32_t pixelFormat = 0, tilingMode = 0;
};

// Opens the window on its own thread. Closing it exits the game.
void StartPresenter();
// Debug hotkeys (F9, F10...) pressed in the game window, as SDL keycodes.
void SetHotkeyHandler(void (*handler)(int key));
// Runs the hotkey handler as if `key` was pressed (remote-controlled test runs).
void TriggerHotkey(int key);
void SetDisplayBuffer(int index, const DisplayBuffer& buffer);
void PresentBuffer(int index);

// A finished frame from the GPU renderer (BGRA8, tightly packed). Once frames
// arrive this way, they're shown instead of the raw display buffers.
// Writes the next `count` presented frames to analysis\frames (quarter size), with
// their hand-over times in times.txt (also LBP3_DUMP_FRAMES=skip:count at startup).
void RequestFrameDump(int count);

// Hands over a frame; pixels comes back holding an older buffer to reuse.
void PresentPixels(std::vector<uint8_t>& pixels, uint32_t width, uint32_t height);

// Frame generation (video/framegen.h, LBP3_FRAMEGEN=1): frames with display times. Once the
// first one arrives the window shows these instead of PresentPixels' frames: at each refresh,
// the latest queued frame that is due by the next refresh.
struct TimedFrame {
    std::vector<uint8_t> pixels;  // BGRA8, tightly packed
    uint32_t width = 0, height = 0;
    int64_t dueNs = 0;     // steady-clock time it should be on screen
    int64_t slotNs = 0;    // the flip slot of the real frame it belongs to (for the timing log)
    int64_t queuedNs = 0;  // when it was queued
    char kind = 'N';       // 'N' a real frame, 'G' a generated one
};
// Queues the frame (its pixels are moved out; frame.pixels is left empty).
void PresentTimed(TimedFrame& frame);
// Takes out every queued frame due by wakeNs + refreshNs - 1 ms and puts the latest in out
// (the older ones count in *skipped and their buffers go back to the spares). False = none due.
// The presenter calls it at each refresh; the offline test calls it directly.
bool PopDue(int64_t wakeNs, int64_t refreshNs, TimedFrame& out, int* skipped);
// Spare frame buffers, recycled between the frame generator and the window.
std::vector<uint8_t> TakeBuffer();
void ReturnBuffer(std::vector<uint8_t>&& buffer);
// The window's display refresh rate (0 until known).
double RefreshHz();
// Frames shown since the last call, real and generated, and timed frames skipped (two due at once).
void TakePresentCounts(uint64_t& shownReal, uint64_t& shownGenerated, uint64_t& skipped);

// LBP3_FRAMEGEN_LOG=1 (with or without frame generation): one row per new frame on screen in
// analysis\framegen.csv: its kind (R = a real frame without frame generation, N, G), its slot,
// due, queue, present and scan-out times, for cadence and latency.

// Blocks until the next flip slot on the window's real vsync for a game running
// at gameHz (e.g. every 2nd refresh for 30 fps on 60 Hz). Returns false when
// that doesn't divide evenly or the window isn't presenting; the caller then
// paces with a timer.
bool WaitForFlipSlot(double gameHz, int64_t* slotNs = nullptr);

// LBP3_PACING: what counts as a display refresh. vblank (default) = the window's DXGI
// output; present = each SDL_RenderPresent returning (upstream); timer = no vsync, the
// caller's 60 Hz timer grid.
enum class Pacing { VBlank, Present, Timer };
Pacing PacingMode();
const char* PacingName();
uint64_t VBlankCount();

}  // namespace video
