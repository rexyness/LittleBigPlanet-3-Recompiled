// libSceImeDialog: the system text-entry dialog (naming things, chat, notes...). On a
// PS4 the system draws a keyboard over the game. Here each dialog gets one of two front
// ends: the on-screen keyboard over the game window (ui/osk_overlay.h), played with the
// pad like the PS4's, whenever a game window is presenting; otherwise (the test
// programs, or LBP3_IME=window) a native Windows window with an edit box, where the PC
// keyboard and Windows' own IME (CJK) work. See ime_dialog.cpp for how LittleBigPlanet 3
// drives it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "hle/abi.h"
#include "ui/osk_overlay.h"

namespace hle::ime_dialog {

// SceImeDialogStatus
enum Status : int32_t { kStatusNone = 0, kStatusRunning = 1, kStatusFinished = 2 };
// SceImeDialogEndStatus
enum EndStatus : int32_t { kEndOk = 0, kEndUserCanceled = 1, kEndAborted = 2 };

// SceIme error codes the dialog returns.
constexpr int kErrorBusy = static_cast<int>(0x80BC0001);
constexpr int kErrorInvalidMaxTextLength = static_cast<int>(0x80BC0016);
constexpr int kErrorInvalidInputTextBuffer = static_cast<int>(0x80BC0017);
constexpr int kErrorInvalidAddress = static_cast<int>(0x80BC0031);
constexpr int kErrorDialogNotRunning = static_cast<int>(0x80BC0105);
constexpr int kErrorDialogNotFinished = static_cast<int>(0x80BC0106);
constexpr int kErrorDialogNotInUse = static_cast<int>(0x80BC0107);

constexpr uint32_t kMaxTextLength = 2048;  // SCE_IME_DIALOG_MAX_TEXT_LENGTH
constexpr uint32_t kOptionMultiline = 0x1;
constexpr uint32_t kOptionPassword = 0x4;
constexpr uint32_t kTypeNumber = 4;

// SceImeDialogParam. Text is UTF-16; lengths count UTF-16 units, without the terminator.
struct Param {
    int32_t userId;
    uint32_t type;                // SceImeType: 0 default, 1 basic Latin, 2 URL, 3 mail, 4 number
    uint64_t supportedLanguages;  // SceImeLanguage bits
    uint32_t enterLabel;
    uint32_t inputMethod;
    void* filter;  // SceImeTextFilter: game code, never called from here (LBP3 passes none)
    uint32_t option;  // SceImeOption bits
    uint32_t maxTextLength;
    char16_t* inputTextBuffer;  // in: prefill; out: the text, at most maxTextLength + 1 units
    float posx, posy;
    uint32_t horizontalAlignment, verticalAlignment;
    const char16_t* placeholder;
    const char16_t* title;
    int8_t reserved[16];
};
static_assert(sizeof(Param) == 0x60, "SceImeDialogParam layout mismatch");
static_assert(offsetof(Param, maxTextLength) == 0x24 && offsetof(Param, inputTextBuffer) == 0x28 &&
                  offsetof(Param, title) == 0x48,
              "SceImeDialogParam layout mismatch");

// SceImeDialogResult
struct Result {
    int32_t endStatus;
    int8_t reserved[12];
};
static_assert(sizeof(Result) == 0x10, "SceImeDialogResult layout mismatch");

int PS4_SYSV_ABI sceImeDialogInit(const Param* param, const void* extended);
int PS4_SYSV_ABI sceImeDialogGetStatus();
int PS4_SYSV_ABI sceImeDialogGetResult(Result* result);
int PS4_SYSV_ABI sceImeDialogAbort();
int PS4_SYSV_ABI sceImeDialogTerm();

// The dialog window's class name, for finding it (tests).
inline constexpr wchar_t kWindowClass[] = L"LBP3ImeDialog";

// Scripted input for automated runs (the remote control's "ime" commands). Each call
// queues one action for the open dialog, or the next one to open: ScriptConfirm puts
// `utf8` in the edit box in place of what's there and confirms it, as if typed and OK
// pressed; ScriptCancel cancels. Safe from any thread.
void ScriptConfirm(const std::string& utf8);
void ScriptCancel();

// Test seam: the on-screen keyboard as the dialog sees it. nullptr (the default) means the
// real one in ui/osk_overlay.h. Set it before opening a dialog, not while one is open.
struct KeyboardHooks {
    bool (*available)();
    uint64_t (*show)(::ui::osk::Request request, ::ui::osk::AnswerFn onAnswer);
    void (*close)(uint64_t id);
};
void SetKeyboardForTest(const KeyboardHooks* hooks);

}  // namespace hle::ime_dialog
