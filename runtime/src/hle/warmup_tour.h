// Warm-up tour (the "shader compilation step", perf/warmup_step.md): on a test save, visit every
// level of the story adventure once so the shader and pipeline caches fill before the player
// gets there. LBP3_WARMUP_TOUR=1 (or "a-b,c,...", index ranges into the level list, visited in that order) runs it after
// the profile has loaded; it refuses to start unless saves.sh has marked the live saves as test
// saves (analysis\savedata_live_is_test). Each level is launched with the game's own "go to level"
// (FUN_002689c0 through FUN_007c6780, as GoToTutorialLevel__ does), then the tour waits for the
// load and dwells: cutscene levels play until the game leaves them, other levels get scripted
// movement (run right and left, jump) for LBP3_WARMUP_TOUR_DWELL seconds (default 150);
// LBP3_WARMUP_TOUR_CUTSCENE caps the wait in a cutscene level (default 300 s).
// Progress goes to the log as [tour] lines; "[tour] done" ends it.
// Remote commands (LBP3_REMOTE_INPUT): "tour-list", "tour-where", "tour-goto <type> <id> [mode]".
#pragma once

#include <istream>
#include <string>

#include "hle/pad_types.h"

namespace hle::warmup_tour {

// Reads LBP3_WARMUP_TOUR (call before the game starts).
void InstallHooks();
// Player 1's pad, every read (no game code): the tour's scripted input while it moves.
void ApplyInput(ps4::PadData& out);
// Once a frame on the game thread (player 1's pad read), not while a system dialog has the pad.
void Tick();
// A remote command starting with "tour-"; the rest of the line is in `in`.
void Command(const std::string& word, std::istream& in);

// The movement pattern on its own (tests): stick x (-1..1) and whether Cross is held, `ms`
// milliseconds into a level's dwell; cutscene levels only tap Cross now and then.
struct Move {
    float stickX;
    bool cross;
};
Move Pattern(uint64_t ms, bool cutscene);

}  // namespace hle::warmup_tour
