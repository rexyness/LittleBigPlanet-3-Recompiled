// Live numbers for the in-game menu's stats (ui/menu_overlay.h): game frames, the work in each,
// new frames on screen (real and generated) and the display latency, over the last second.
// Cheap: a lock per call, 30-60 calls a second.
#pragma once

namespace video::live {

// The game's flip (hle/videoout.cpp): `workMs` between the last slot and this one's start,
// `intervalMs` since the last flip.
void NoteFlip(double workMs, double intervalMs);
// A new frame on screen (video/presenter.cpp). `latencyMs`: from the game handing a real frame
// over (its flip slot with frame generation) to its present; < 0 for generated frames.
void NoteShown(bool generated, double latencyMs);

struct Snapshot {
    bool valid = false;        // at least one full window seen
    double gameFps = 0;        // game frames (flips) per second
    double frameMs = 0;        // average flip interval
    double worstFrameMs = 0;
    double workMs = 0;         // average work per game frame
    double shownFps = 0;       // new frames on screen per second (real + generated)
    double generatedShare = 0; // generated / shown
    double latencyMs = -1;     // average, real frames; < 0 if none
};
Snapshot Read();

}  // namespace video::live
