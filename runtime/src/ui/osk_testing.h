// Test hooks into the on-screen keyboard overlay (osktest only; nothing in the game uses
// them). Presenter-thread rules apply: call them from the thread that calls Draw.
#pragma once
#include <cstdint>
#include <functional>

#include "hle/pad_types.h"
#include "ui/osk_model.h"

namespace ui::osk::testing {

// Runs fn on the keyboard that is up. False if none is (call Draw once after Show first).
bool WithKeyboard(const std::function<void(Keyboard&)>& fn);
// The pad the overlay reads instead of input::Read(0), until cleared with nullptr.
void SetPad(const ps4::PadData* pad);
// A millisecond clock for repeats and the caret blink instead of SDL_GetTicks (nullptr = real).
void SetClock(uint64_t (*nowMs)());

}  // namespace ui::osk::testing
