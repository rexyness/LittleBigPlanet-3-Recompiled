// Test hooks into the system-dialog overlay (dlgtest only; nothing in the game uses them).
// Presenter-thread rules apply: call them from the thread that calls ui::overlay::Draw.
#pragma once
#include <cstddef>
#include <cstdint>

#include "hle/pad_types.h"

namespace ui::dialog::testing {

struct State {
    uint64_t id = 0;
    int focus = 0;          // the focused button (messages, progress)
    int selected = -1;      // lists: the selected entry
    int firstRow = 0;       // lists: the first visible entry
    uint32_t progress = 0;  // progress dialogs
};

// The dialog taking input, once a Draw (or HandleEvent) has picked it up. False if none is up.
bool Top(State& out);
// How many dialogs are up on the presenter side.
size_t LiveCount();
// Where the last drawn frame put the top dialog's parts, in window coordinates: a button
// (1 or 2), a list entry (its index, if visible), the Circle hint. False if not drawn.
bool ButtonCentre(int button, float& x, float& y);
bool RowCentre(int index, float& x, float& y);
bool BackHintCentre(float& x, float& y);
// The pad the dialogs read instead of input::Read(0), until cleared with nullptr.
void SetPad(const ps4::PadData* pad);
// A millisecond clock for repeats and double clicks instead of SDL_GetTicks (nullptr = real).
void SetClock(uint64_t (*nowMs)());

}  // namespace ui::dialog::testing
