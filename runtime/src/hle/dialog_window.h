// Native Windows windows for the PS4 system dialogs (hle/common_dialog.cpp).
// On a PS4 the system draws these over the game while the game keeps running
// and polls for the answer. Here each dialog is a small Win32 dialog on its own
// thread, centred over the game window; the game's UpdateStatus calls Poll,
// which also lets the gamepad answer it (Cross presses the focused button,
// which starts on the default one; Circle cancels; the D-pad moves the focus
// or the list selection). Mouse and keyboard work as in any Windows dialog.
//
// LBP3_AUTOPRESS (scripted runs) taps buttons on the pad blindly, so dialogs
// ignore the pad then and, after a moment, press Spec::autoAnswer instead: the
// caller sets it to the choice that changes nothing (No, Cancel, or OK where
// OK is the only answer).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hle::dialog_ui {

enum class Kind { Message, List, Progress };
enum class Icon { None, Info, Question, Warning, Error };

// Spec::cancelButton value for "Circle/Esc close it without pressing a button".
constexpr int kDismiss = -1;

struct Spec {
    Kind kind = Kind::Message;
    std::wstring text;              // message, list prompt or progress caption
    Icon icon = Icon::Info;
    std::wstring button1, button2;  // labels, left to right; empty = no such button
    int defaultButton = 1;          // 1 or 2: focused at first, so Cross and Enter press it
    int cancelButton = 0;           // what Circle, Esc and the close box do: 1, 2, kDismiss or 0 (nothing)
    std::vector<std::wstring> items;  // Kind::List entries
    int focus = 0;                    // Kind::List: entry selected at first
    int autoAnswer = 0;  // under LBP3_AUTOPRESS: 1, 2 or kDismiss, pressed after a moment; 0 = leave it to the game
    const char* logName = "dialog";   // e.g. "msg", "savedata"
};

struct Outcome {
    int button = 0;  // 1 or 2 = that button; kDismiss = cancelled without a button; 0 = closed by the game
    int item = -1;   // Kind::List: entry selected when button 1 was pressed
};

class Window;

// Opens the dialog on a new thread. Never blocks.
std::shared_ptr<Window> Show(Spec spec);
// Called from UpdateStatus (any thread, any rate): handles the pad and LBP3_AUTOPRESS.
// True once the dialog has been answered, with the answer in `out`.
bool Poll(Window& w, Outcome& out);
// Progress dialogs: 0..100.
void SetProgress(Window& w, uint32_t percent);
// The game closes the dialog. Does nothing once it has been answered.
void Close(Window& w);

// For tests.
void* NativeHandle(const Window& w);              // HWND, once the window exists
using PadReader = bool (*)(uint32_t& buttons);    // returns false if no pad
void SetPadReaderForTest(PadReader reader);       // nullptr = the real pad (input::Read)
void SetAutoPressForTest(int mode);               // -1 = from LBP3_AUTOPRESS, 0 = off, 1 = on
void SetAutoAnswerDelayForTest(uint32_t ms);

}  // namespace hle::dialog_ui
