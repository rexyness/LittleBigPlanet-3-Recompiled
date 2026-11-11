// The on-screen keyboard: a PS4-style text-entry panel drawn over the game window with
// Dear ImGui on the presenter's SDL renderer, played with the pad (or the mouse and a
// physical keyboard). libSceImeDialog (hle/ime_dialog.cpp) is its only user; without a
// game window (the test programs) that falls back to its native window.
//
// Threads: Show, Close, Visible and Available may be called from any thread. Attach,
// HandleEvent and Draw run on the presenter thread only, and every ImGui call stays there.
// The answer callback also runs on the presenter thread, so it must not call game code.
//
// Controls (the PS4's, as shadPS4's keyboard lays them out): d-pad or left stick = move,
// Cross or a touch-pad click = press the key, Circle = cancel, R2 = done, Square =
// backspace, Triangle = space, L2 = shift, L2+Triangle = symbols, L3 = accents,
// L1/R1 = move the text cursor. Physical keys: typing, Backspace, Delete, Left/Right,
// Home/End, Enter = done, Esc = cancel. Buttons and keys already held when the keyboard
// opens are ignored until released.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

union SDL_Event;
struct SDL_Window;
struct SDL_Renderer;

namespace ui::osk {

struct Request {
    std::u16string title;        // the game's title, BOM already stripped
    std::u16string text;         // prefill
    std::u16string placeholder;  // shown greyed while the text is empty
    uint32_t maxLength = 0;      // UTF-16 units, 1..2048
    bool multiline = false;      // NewLine key and Enter insert a line break
    bool password = false;       // draw the text as dots
    uint32_t enterLabel = 0;     // SceImeEnterLabel: 0 default ("Done"), 1 "Send", 2 "Search", 3 "Go"
};

struct Answer {
    bool confirmed = false;  // false = cancelled (Circle, Esc, the close button)
    std::u16string text;     // the confirmed text, at most maxLength units
};

// Runs at most once, on the presenter thread, when the player confirms or cancels. Not for a
// keyboard already closed or replaced; but a Close that races the player's answer can return
// just before this runs, so a caller that closed must ignore a late answer (ime_dialog does).
using AnswerFn = std::function<void(const Answer&)>;

// True once a game window is presenting (Attach ran).
bool Available();
// Puts the keyboard up. One at a time: a second Show replaces the first without answering
// it. Returns an id for Close (never 0).
uint64_t Show(Request request, AnswerFn onAnswer);
// Takes the keyboard down without answering (the game aborted, or a script answered).
// A stale id (that keyboard already answered or was replaced) is ignored.
void Close(uint64_t id);
// True while a keyboard is up.
bool Visible();

// ---- Presenter thread only ----
void Attach(SDL_Window* window, SDL_Renderer* renderer);
// Every window event goes through here first; true = the keyboard used it, so it is not
// a game hotkey. Always false while no keyboard is up.
bool HandleEvent(const SDL_Event& e);
// Between drawing the game frame and SDL_RenderPresent: reads the pad, updates and draws
// the keyboard. One atomic load when none is up.
void Draw();

}  // namespace ui::osk
