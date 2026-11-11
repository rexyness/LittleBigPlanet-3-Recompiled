// The PS4 system dialogs (message, saved-data list, progress) drawn over the game window with
// Dear ImGui, in the on-screen keyboard's style (ui/osk_overlay.h). hle/dialog_window.cpp is
// its only user: it picks this when Available() and LBP3_DIALOGS isn't "window", and keeps
// its native Win32 windows otherwise (the test programs).
//
// Threads: Show, SetProgress, Close, Visible and Available may be called from any thread.
// Everything ImGui runs on the presenter thread (ui/overlay host). The answer callback runs
// on the presenter thread too, so it must not call game code.
//
// Controls, as dialog_window.cpp's native windows: Cross presses the focused button (which
// starts on defaultButton); Circle does what cancelButton says; for a message the d-pad moves
// the button focus (left/up = button 1, right/down = button 2); for a list up/down move the
// selection (with repeat) and Cross presses button 1 with the selected item. Enter, Esc and
// the arrow keys do the same on the PC keyboard; the mouse can click buttons and rows.
// Buttons and keys already held when a dialog opens count only after a release.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ui::dialog {

enum class Kind { Message, List, Progress };
enum class Icon { None, Info, Question, Warning, Error };

// Request::cancelButton value for "Circle/Esc close it without pressing a button".
constexpr int kDismiss = -1;

struct Item {
    std::wstring label;     // e.g. the save's folder name and date, or "[New saved data]  ..."
    std::wstring iconPath;  // a PNG drawn beside the label (the save's ICON0.PNG); empty = none
};

struct Request {
    Kind kind = Kind::Message;
    std::wstring text;              // message, list prompt or progress caption
    Icon icon = Icon::Info;
    std::wstring button1, button2;  // labels, left to right; empty = no such button
    int defaultButton = 1;          // 1 or 2: focused at first
    int cancelButton = 0;           // what Circle and Esc do: 1, 2, kDismiss or 0 (nothing)
    std::vector<Item> items;        // Kind::List entries
    int focus = 0;                  // Kind::List: entry selected at first
    uint32_t progress = 0;          // Kind::Progress: 0..100 at open
    bool padIgnored = false;        // LBP3_AUTOPRESS runs: the pad's presses are synthetic; the
                                    // caller answers for it, so the dialog ignores the pad
    const char* logName = "dialog";  // e.g. "msg", "savedata"
};

struct Answer {
    int button = 0;  // 1 or 2 = that button; kDismiss = cancelled without a button
    int item = -1;   // Kind::List: entry selected when button 1 was pressed
};

// Runs at most once, on the presenter thread, when the player answers. Not for a dialog
// already closed or replaced; but a Close that races the player's answer can return just
// before this runs, so a caller that closed must ignore a late answer.
using AnswerFn = std::function<void(const Answer&)>;

// True once a game window is presenting.
bool Available();
// Puts a dialog up and returns its id (never 0). Several may be up; the newest takes the
// input and is drawn on top.
uint64_t Show(Request request, AnswerFn onAnswer);
// Kind::Progress: 0..100. Stale ids are ignored.
void SetProgress(uint64_t id, uint32_t percent);
// Takes a dialog down without answering (the game closed it, or a script answered).
void Close(uint64_t id);
// True while any dialog is up.
bool Visible();

}  // namespace ui::dialog
