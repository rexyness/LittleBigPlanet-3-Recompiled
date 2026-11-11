// Milestone 5 measurement: where each frame's time goes. With LBP3_FRAME_SPLIT=1 (or
// LBP3_FIRST_FRAME=1, which adds GPU timestamps around uploads) every flipped frame becomes one
// row of analysis\frame_split.csv (columns: frame_split.cpp). Off, every call is a branch on a
// static bool.
//
// A frame is followed by an id from the game's main thread, which starts it in
// sceGnmSubmitAndFlipCommandBuffers (BeginFrame), to the command-processor thread, which
// reaches its flip packet (SetCpFrame, from the pending-flip queue). Each thread marks points
// on the frame it is working on (Mark). The row is written once both the main thread has been
// released (Released) and the frame has been flipped (Flipped), in whichever order: when the
// graphics queue blocks at the start of a frame, the game is released early and the command
// processor finishes the frame later.
#pragma once
#include <cstdint>

namespace gpu::split {

enum Point : int {
    SubmitEnter,  // main: entered sceGnmSubmitAndFlipCommandBuffers
    WaitIdleEnd,  // main: the wait for the previous frame's queues (gpu::WaitIdle) returned
    Enqueued,     // main: this frame's command buffer handed to the command processor
    FlipIrq,      // command processor: reached the frame's flip packet (its draws recorded)
    FinishBegin,  // command processor: the frame's Vulkan submit and GPU wait begin
    FinishEnd,    //   ... the GPU has run the frame
    CopyEnd,      // command processor: the display image copied back to the CPU
    SlotBegin,    // command processor: waiting for the flip's slot (vblank or timer grid)
    SlotEnd,
    Flipped,      // command processor: frame handed to the window and the flip reported
    Released,     // main: the submit's wait returned; the game's main thread runs on
    kPoints
};

bool Enabled();
// Main thread, entering sceGnmSubmitAndFlipCommandBuffers: a new frame id (0 when off), which
// later Mark calls on this thread use, marked SubmitEnter.
uint64_t BeginFrame();
// Command-processor thread: the frame its following Mark calls belong to (0 = none).
void SetCpFrame(uint64_t frame);
// Marks a point now, on the frame the calling thread is working on.
void Mark(Point p);
// Main thread, just before Mark(Released): how the two waits returned (gpu::WaitResult values).
void SetResults(int waitIdleResult, int submitResult);
// Main thread: a sceGnmSubmitCommandBuffers call (no flip) took `us` for `buffers` command
// buffers of `dwords` in all; counted into the next frame's row.
void NoteSubmit(int64_t us, uint32_t buffers, uint32_t dwords);
// Main thread, in the flip submit after BeginFrame: its command buffers and their dwords in all.
void SetFlipBuffers(uint32_t buffers, uint32_t dwords);
// Writes buffered rows to the file (the status line calls it every 30 flips).
void Flush();

}  // namespace gpu::split
