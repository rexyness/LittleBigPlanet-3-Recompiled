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
//
// Front end: while the game window is presenting, a dialog is drawn over it with
// Dear ImGui instead (ui/dialog_overlay.h), with the same buttons and controls;
// LBP3_DIALOGS=window keeps the native windows. The overlay reads the pad itself,
// and its answer is recorded here for Poll to hand to the game thread. Each
// dialog logs which one it opened on ("[dialog] <name>: on the overlay" or
// "... on a window").
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ui/dialog_overlay.h"

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
    std::vector<std::wstring> itemIcons;  // Kind::List: a PNG per entry (the save's ICON0.PNG), parallel
                                          // to items; empty = none. Only the overlay draws them.
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
void* NativeHandle(const Window& w);              // HWND, once the window exists; nullptr on the overlay
bool OnOverlay(const Window& w);                  // true if this dialog went to the ImGui overlay
using PadReader = bool (*)(uint32_t& buttons);    // returns false if no pad
void SetPadReaderForTest(PadReader reader);       // nullptr = the real pad (input::Read)
void SetAutoPressForTest(int mode);               // -1 = from LBP3_AUTOPRESS, 0 = off, 1 = on
void SetAutoAnswerDelayForTest(uint32_t ms);
// Stand-ins for ui::dialog's functions, so the overlay path runs without a game window;
// nullptr = the real ones. Every member must be set.
struct OverlayForTest {
    bool (*available)();
    uint64_t (*show)(ui::dialog::Request request, ui::dialog::AnswerFn onAnswer);
    void (*setProgress)(uint64_t id, uint32_t percent);
    void (*close)(uint64_t id);
};
void SetOverlayForTest(const OverlayForTest* overlay);
void SetFrontEndForTest(int mode);  // -1 = from LBP3_DIALOGS, 0 = the overlay when available, 1 = windows

}  // namespace hle::dialog_ui
