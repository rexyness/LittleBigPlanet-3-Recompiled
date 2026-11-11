// Offline checks for the HLE libSceImeDialog. Calls it the way LittleBigPlanet 3 does
// (the param built as at eboot.bin+0x559060, polled and finished as at +0x559800) and
// drives the dialog window with window messages, the scripted API and the remote
// control's "ime" commands. The window opens briefly and never takes the foreground.
// Prints each failed check; exit code 0 = all passed.
//
// The pad cases run in child processes (imetest --pad cross|circle): they feed the
// pad through input::Read with LBP3_AUTOPRESS, which is read once per process.
//
// imetest --manual: per-monitor DPI aware like the game (SDL3), opens one dialog as the
// game's text chat would and waits for a person to confirm or cancel it; for trying
// the layout, typing and Windows' IME by hand.
//
// The on-screen keyboard (ui/osk_overlay.h) needs the game's window, so here it is a fake
// put in through SetKeyboardForTest: these checks cover the dialog's side of it (the front
// end chosen, the request, answers, scripts, abort), not the keyboard itself. Child runs:
// imetest --pad-keyboard cross|circle (LBP3_AUTOPRESS with the keyboard up) and
// imetest --ime-window (LBP3_IME=window keeps the window though a keyboard is available).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hle/ime_dialog.h"
#include "hle/modal.h"
#include "input/gamepad.h"

namespace input::remote {
void Parse(const std::string& line);  // gamepad_sdl.cpp: one line of analysis\input_cmd.txt
}

using namespace hle::ime_dialog;

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL (line %d): %s\n", line, what);
    }
}
#define CHECK(c) Check((c), #c, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)

// The game's text buffer (0x201 UTF-16 units at 0x13abed0), plus a guard tail: nothing
// past maxTextLength + 1 units may be written.
constexpr char16_t kGuard = 0xAAAA;
char16_t g_buffer[0x201 + 16];

void Prefill(const std::u16string& text) {
    std::fill(std::begin(g_buffer), std::end(g_buffer), kGuard);
    std::copy(text.begin(), text.end(), g_buffer);
    g_buffer[text.size()] = 0;
}
std::u16string Buffer() { return g_buffer; }
bool GuardFrom(size_t i) {
    return std::all_of(g_buffer + i, std::end(g_buffer), [](char16_t c) { return c == kGuard; });
}

// The param exactly as LBP3 fills it: zeroed, then these fields.
Param GameParam(uint32_t maxLength) {
    Param p;
    std::memset(&p, 0, sizeof(p));
    p.userId = 1;
    p.supportedLanguages = 0x4;  // ENGLISH_US: one bit, from the system language
    p.maxTextLength = maxLength;
    p.inputTextBuffer = g_buffer;
    p.posx = 960.f;
    p.posy = 540.f;
    p.horizontalAlignment = 1;  // centre
    p.verticalAlignment = 1;
    p.title = u"﻿Enter Your Message";  // the game's localised strings keep their BOM
    return p;
}
int Open(uint32_t maxLength) {
    const Param p = GameParam(maxLength);
    return sceImeDialogInit(&p, nullptr);
}

template <typename F>
bool WaitFor(F done, int ms = 5000) {
    for (int t = 0; t < ms; t += 5) {
        if (done()) return true;
        Sleep(5);
    }
    return done();
}

HWND FindDialog() {  // our process's dialog window, if one is up
    for (HWND h = nullptr; (h = FindWindowExW(nullptr, h, kWindowClass, nullptr));) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(h)) return h;
    }
    return nullptr;
}
HWND WaitDialog() {
    HWND h = nullptr;
    WaitFor([&] { return (h = FindDialog()) != nullptr; });
    return h;
}
HWND EditOf(HWND dialog) { return FindWindowExW(dialog, nullptr, L"Edit", nullptr); }
std::wstring Text(HWND h) {
    wchar_t buf[4096] = {};
    GetWindowTextW(h, buf, 4096);
    return buf;
}
std::wstring W(const std::u16string& s) { return std::wstring(s.begin(), s.end()); }
std::string Utf8(const std::u16string& s) {
    const std::wstring w = W(s);
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}
void SetText(HWND edit, const std::wstring& text) {
    SendMessageW(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
}
// What a button sends its parent when clicked.
void Press(HWND dialog, int id) {
    PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dialog, id)));
}
void Key(HWND target, UINT vk, UINT scan) {
    PostMessageW(target, WM_KEYDOWN, vk, 1 | (scan << 16));
    PostMessageW(target, WM_KEYUP, vk, 1 | (scan << 16) | 0xC0000000u);
}

// The game's frame loop: GetStatus every frame until FINISHED.
bool PollUntilFinished() {
    return WaitFor([] { return sceImeDialogGetStatus() == kStatusFinished; });
}

// The game's handling of a finished dialog (+0x559800): GetResult, then Term. Also
// checks that everything is back to idle: status, modal gate, window.
int32_t GameFinish() {
    Result r;
    std::memset(&r, 0, sizeof(r));
    r.endStatus = -1;  // (the game leaves 0 here; -1 shows a GetResult that wrote nothing)
    CHECK_EQ(sceImeDialogGetResult(&r), 0);
    CHECK_EQ(sceImeDialogTerm(), 0);
    CHECK_EQ(sceImeDialogGetStatus(), kStatusNone);
    // The gate comes down on that poll (no pad here), unless Enter or Esc is down on
    // this PC's keyboard, which is read globally: then within the dialog's 1 s cap.
    CHECK(WaitFor([] { return sceImeDialogGetStatus() == kStatusNone && !hle::modal::Active(); }, 1500));
    CHECK(WaitFor([] { return FindDialog() == nullptr; }));
    return r.endStatus;
}

// Back to idle whatever a failed test left behind.
void Reset() {
    if (sceImeDialogGetStatus() != kStatusNone) {
        sceImeDialogAbort();
        Result r{};
        sceImeDialogGetResult(&r);
        sceImeDialogTerm();
    }
    sceImeDialogGetStatus();
    WaitFor([] { return FindDialog() == nullptr; });
}

// ---- The on-screen keyboard, faked ----

struct FakeKeyboard {
    std::mutex mutex;
    bool available = true;
    int shows = 0;
    uint64_t nextId = 1, shownId = 0;
    ui::osk::Request request;
    ui::osk::AnswerFn answer;
    std::vector<uint64_t> closed;
};
FakeKeyboard g_fake;

const KeyboardHooks kFakeKeyboard{
    [] {
        std::lock_guard lock(g_fake.mutex);
        return g_fake.available;
    },
    [](ui::osk::Request request, ui::osk::AnswerFn answer) -> uint64_t {
        std::lock_guard lock(g_fake.mutex);
        ++g_fake.shows;
        g_fake.request = std::move(request);
        g_fake.answer = std::move(answer);
        return g_fake.shownId = g_fake.nextId++;
    },
    [](uint64_t id) {
        std::lock_guard lock(g_fake.mutex);
        g_fake.closed.push_back(id);
    },
};

void UseFakeKeyboard(bool available) {
    {
        std::lock_guard lock(g_fake.mutex);
        g_fake.available = available;
        g_fake.shows = 0;
        g_fake.shownId = 0;
        g_fake.request = {};
        g_fake.answer = nullptr;
        g_fake.closed.clear();
    }
    SetKeyboardForTest(&kFakeKeyboard);
}
int FakeShows() {
    std::lock_guard lock(g_fake.mutex);
    return g_fake.shows;
}
uint64_t FakeShownId() {
    std::lock_guard lock(g_fake.mutex);
    return g_fake.shownId;
}
bool FakeClosed(uint64_t id) {
    std::lock_guard lock(g_fake.mutex);
    return std::find(g_fake.closed.begin(), g_fake.closed.end(), id) != g_fake.closed.end();
}
size_t FakeCloseCount() {
    std::lock_guard lock(g_fake.mutex);
    return g_fake.closed.size();
}
// The player answers on the keyboard: the callback runs on another thread, as on the presenter's.
bool FakeAnswer(bool confirmed, std::u16string text) {
    ui::osk::AnswerFn answer;
    {
        std::lock_guard lock(g_fake.mutex);
        answer = g_fake.answer;
    }
    if (!answer) return false;
    std::thread([&] { answer({confirmed, std::move(text)}); }).join();
    return true;
}

void TestIdle() {
    std::puts("idle: NONE, nothing to get, abort or terminate");
    Reset();
    bool none = true;
    for (int i = 0; i < 10000; ++i) none &= sceImeDialogGetStatus() == kStatusNone;
    CHECK(none);
    Result r{};
    CHECK_EQ(sceImeDialogGetResult(&r), kErrorDialogNotInUse);
    CHECK_EQ(sceImeDialogAbort(), kErrorDialogNotInUse);
    CHECK_EQ(sceImeDialogTerm(), kErrorDialogNotInUse);
    CHECK(!hle::modal::Active());
    CHECK(FindDialog() == nullptr);
}

void TestBadParams() {
    std::puts("bad params are refused and open nothing");
    Reset();
    Prefill(u"Hello");
    CHECK_EQ(sceImeDialogInit(nullptr, nullptr), kErrorInvalidAddress);
    Param p = GameParam(96);
    p.inputTextBuffer = nullptr;
    CHECK_EQ(sceImeDialogInit(&p, nullptr), kErrorInvalidInputTextBuffer);
    CHECK_EQ(Open(0), kErrorInvalidMaxTextLength);
    CHECK_EQ(sceImeDialogGetStatus(), kStatusNone);
    CHECK(!hle::modal::Active());
}

void TestMaxLengthClamped() {
    std::puts("a max length over 2048 is clamped, as the firmware does, not refused");
    Reset();
    Prefill(u"Hello");
    CHECK_EQ(Open(kMaxTextLength + 1), 0);
    HWND dialog = WaitDialog();
    CHECK(dialog != nullptr);
    if (dialog) CHECK_EQ(SendMessageW(EditOf(dialog), EM_GETLIMITTEXT, 0, 0), kMaxTextLength);
    CHECK_EQ(sceImeDialogAbort(), 0);
    Reset();
    CHECK_EQ(Buffer(), u"Hello");  // aborted: nothing written
}

void TestScriptedConfirm() {
    std::puts("scripted confirm: NONE -> RUNNING -> FINISHED -> NONE, text written back");
    Reset();
    Prefill(u"Hello");
    ScriptConfirm("Scripted text");
    CHECK_EQ(sceImeDialogGetStatus(), kStatusNone);  // a queued script opens nothing
    CHECK_EQ(Open(96), 0);
    CHECK_EQ(sceImeDialogGetStatus(), kStatusRunning);
    CHECK(hle::modal::Active());
    CHECK(PollUntilFinished());
    CHECK_EQ(Buffer(), u"Scripted text");  // written by the time FINISHED is reported
    // FINISHED but not terminated: still busy, and nothing to abort
    CHECK_EQ(Open(96), kErrorBusy);
    CHECK_EQ(sceImeDialogAbort(), kErrorDialogNotRunning);
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"Scripted text");  // Term leaves it alone
    CHECK(GuardFrom(14));
}

void TestWindowConfirm() {
    std::puts("window: prefill and title shown, busy while running, OK confirms");
    Reset();
    Prefill(u"Hello");
    CHECK_EQ(Open(96), 0);
    HWND dialog = WaitDialog();
    CHECK(dialog != nullptr);
    if (!dialog) return;
    HWND edit = EditOf(dialog);
    CHECK(Text(edit) == L"Hello");
    CHECK(Text(FindWindowExW(dialog, nullptr, L"Static", nullptr)) == L"Enter Your Message");  // BOM gone
    CHECK(Text(GetDlgItem(dialog, IDOK)) == L"OK");
    CHECK_EQ(SendMessageW(edit, EM_GETLIMITTEXT, 0, 0), 96);
    CHECK_EQ(Open(96), kErrorBusy);
    Result r{};
    CHECK_EQ(sceImeDialogGetResult(&r), kErrorDialogNotFinished);
    CHECK_EQ(sceImeDialogTerm(), kErrorDialogNotFinished);
    CHECK_EQ(sceImeDialogGetStatus(), kStatusRunning);
    CHECK(hle::modal::Active());
    SetText(edit, L"Typed in the window");
    Press(dialog, IDOK);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"Typed in the window");
}

void TestKeysAndCancel() {
    std::puts("window: Enter confirms; Esc, Cancel and the close box cancel and keep the game's text");
    Reset();
    Prefill(u"Hello");
    CHECK_EQ(Open(96), 0);
    HWND dialog = WaitDialog();
    CHECK(dialog != nullptr);
    if (!dialog) return;
    SetText(EditOf(dialog), L"Enter key");
    // Auto-repeat of an Enter or Esc still held from the game window (bit 30: already
    // down) doesn't close the dialog; a fresh Enter does.
    for (UINT vk : {VK_RETURN, VK_ESCAPE})
        PostMessageW(EditOf(dialog), WM_KEYDOWN, vk, 1 | ((vk == VK_RETURN ? 0x1Cu : 0x01u) << 16) | (1u << 30));
    CHECK(!WaitFor([] { return sceImeDialogGetStatus() != kStatusRunning; }, 300));
    Key(EditOf(dialog), VK_RETURN, 0x1C);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"Enter key");

    for (int how = 0; how < 3; ++how) {
        Reset();
        Prefill(u"Keep me");
        CHECK_EQ(Open(96), 0);
        dialog = WaitDialog();
        CHECK(dialog != nullptr);
        if (!dialog) return;
        SetText(EditOf(dialog), L"Discard me");
        if (how == 0) Key(EditOf(dialog), VK_ESCAPE, 0x01);
        if (how == 1) Press(dialog, IDCANCEL);
        if (how == 2) PostMessageW(dialog, WM_CLOSE, 0, 0);
        CHECK(PollUntilFinished());
        CHECK_EQ(GameFinish(), kEndUserCanceled);
        CHECK_EQ(Buffer(), u"Keep me");
        CHECK(GuardFrom(8));
    }
}

void TestScriptedCancel() {
    std::puts("scripted cancel");
    Reset();
    Prefill(u"Keep me");
    ScriptCancel();
    CHECK_EQ(Open(96), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndUserCanceled);
    CHECK_EQ(Buffer(), u"Keep me");
}

void TestAbort() {
    std::puts("abort: FINISHED at once, result ABORTED, buffer kept, Init right after Term works");
    Reset();
    Prefill(u"Original");
    CHECK_EQ(Open(96), 0);
    HWND dialog = WaitDialog();
    CHECK(dialog != nullptr);
    CHECK_EQ(sceImeDialogAbort(), 0);
    CHECK_EQ(sceImeDialogGetStatus(), kStatusFinished);  // the game's next poll
    CHECK_EQ(sceImeDialogAbort(), kErrorDialogNotRunning);
    CHECK_EQ(Open(96), kErrorBusy);
    CHECK_EQ(GameFinish(), kEndAborted);  // (and the window has gone)
    CHECK_EQ(Buffer(), u"Original");

    // Aborted before its window is even up.
    CHECK_EQ(Open(96), 0);
    CHECK_EQ(sceImeDialogAbort(), 0);
    CHECK_EQ(GameFinish(), kEndAborted);
    CHECK_EQ(Buffer(), u"Original");

    // Init straight after Term, while the aborted window may still be closing: the new
    // dialog gets the script, the old one writes nothing.
    CHECK_EQ(Open(96), 0);
    WaitDialog();
    CHECK_EQ(sceImeDialogAbort(), 0);
    Result r{};
    CHECK_EQ(sceImeDialogGetResult(&r), 0);
    CHECK_EQ(r.endStatus, kEndAborted);
    CHECK_EQ(sceImeDialogTerm(), 0);
    ScriptConfirm("After abort");
    CHECK_EQ(Open(96), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"After abort");
}

void TestMaxLength() {
    std::puts("max length: clamped in UTF-16 units, never past max + 1, pairs never split");
    Reset();
    Prefill(u"");
    ScriptConfirm("0123456789ABC");
    CHECK_EQ(Open(8), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"01234567");
    CHECK(GuardFrom(9));

    Prefill(u"");
    CHECK_EQ(Open(8), 0);
    HWND dialog = WaitDialog();
    CHECK(dialog != nullptr);
    if (!dialog) return;
    SetText(EditOf(dialog), L"abcdefghijkl");  // WM_SETTEXT isn't held to the edit's limit
    Press(dialog, IDOK);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"abcdefgh");
    CHECK(GuardFrom(9));

    // A prefill longer than the field shows cut to it (and is kept on cancel).
    Prefill(u"0123456789");
    CHECK_EQ(Open(4), 0);
    dialog = WaitDialog();
    CHECK(dialog != nullptr);
    if (!dialog) return;
    CHECK(Text(EditOf(dialog)) == L"0123");
    Press(dialog, IDCANCEL);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndUserCanceled);
    CHECK_EQ(Buffer(), u"0123456789");

    // U+1F600 is two units; at a limit of 5 it would straddle the end, so it goes whole.
    const std::u16string emoji = u"abcd\U0001F600";
    Prefill(u"");
    ScriptConfirm(Utf8(emoji));
    CHECK_EQ(Open(5), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"abcd");
    CHECK(GuardFrom(6));

    Prefill(u"");
    ScriptConfirm(Utf8(emoji));
    CHECK_EQ(Open(6), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), emoji);
    CHECK(GuardFrom(7));
}

void TestUnicode() {
    std::puts("non-ASCII text round trip (prefill shown, UTF-8 script in, window text in)");
    Reset();
    const std::u16string prefill = u"Grüße 日本語 \U0001F600";
    Prefill(prefill);
    CHECK_EQ(Open(96), 0);
    HWND dialog = WaitDialog();
    CHECK(dialog != nullptr);
    if (!dialog) return;
    CHECK(Text(EditOf(dialog)) == W(prefill));
    const std::u16string typed = u"Größe ½ 東京 \U0001F389";
    ScriptConfirm(Utf8(typed));  // applies to the dialog that's open
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), typed);

    const std::u16string other = u"Ελληνικά "
                                 u"العربية 한국어";
    Prefill(u"");
    CHECK_EQ(Open(96), 0);
    dialog = WaitDialog();
    CHECK(dialog != nullptr);
    if (!dialog) return;
    SetText(EditOf(dialog), W(other));
    Press(dialog, IDOK);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), other);
}

void TestRemoteCommands() {
    std::puts("remote control: \"ime <text>\" and \"ime-cancel\"");
    Reset();
    input::remote::Parse("ime  two spaces, kept as written \r");
    Prefill(u"x");
    CHECK_EQ(Open(96), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u" two spaces, kept as written ");

    const std::u16string cafe = u"Café ☕";
    input::remote::Parse("ime " + Utf8(cafe));
    Prefill(u"x");
    CHECK_EQ(Open(96), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), cafe);

    input::remote::Parse("ime-cancel");
    Prefill(u"x");
    CHECK_EQ(Open(96), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndUserCanceled);
    CHECK_EQ(Buffer(), u"x");
}

void TestKeyboardRequest() {
    std::puts("keyboard: chosen when available, with the game's param in the request; no window");
    Reset();
    UseFakeKeyboard(true);
    Prefill(u"Hello");
    Param p = GameParam(96);
    p.option = kOptionMultiline | kOptionPassword;
    p.enterLabel = 2;  // Search
    p.placeholder = u"Type here";
    CHECK_EQ(sceImeDialogInit(&p, nullptr), 0);
    CHECK_EQ(sceImeDialogGetStatus(), kStatusRunning);
    CHECK(hle::modal::Active());
    CHECK_EQ(FakeShows(), 1);
    {
        std::lock_guard lock(g_fake.mutex);
        CHECK(g_fake.request.title == u"Enter Your Message");  // BOM gone
        CHECK(g_fake.request.text == u"Hello");
        CHECK(g_fake.request.placeholder == u"Type here");
        CHECK_EQ(g_fake.request.maxLength, 96u);
        CHECK(g_fake.request.multiline);
        CHECK(g_fake.request.password);
        CHECK_EQ(g_fake.request.enterLabel, 2u);
    }
    CHECK_EQ(Open(96), kErrorBusy);
    CHECK(!WaitFor([] { return FindDialog() != nullptr; }, 300));  // no window as well
    CHECK(FakeAnswer(true, u"Typed on the keyboard"));
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"Typed on the keyboard");
    CHECK_EQ(FakeCloseCount(), 0u);  // it answered, so it took itself down

    // A max length over 2048 reaches the keyboard clamped.
    CHECK_EQ(Open(kMaxTextLength + 1), 0);
    {
        std::lock_guard lock(g_fake.mutex);
        CHECK_EQ(g_fake.request.maxLength, kMaxTextLength);
        CHECK(!g_fake.request.multiline && !g_fake.request.password);
    }
    CHECK_EQ(sceImeDialogAbort(), 0);
    Reset();
    SetKeyboardForTest(nullptr);
}

void TestKeyboardAnswers() {
    std::puts("keyboard: confirm (clamped), cancel, only the first answer counts, abort closes it");
    Reset();
    UseFakeKeyboard(true);
    Prefill(u"");
    CHECK_EQ(Open(8), 0);
    CHECK(FakeAnswer(true, u"abcdefghijkl"));
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"abcdefgh");
    CHECK(GuardFrom(9));

    Prefill(u"Keep me");
    CHECK_EQ(Open(96), 0);
    CHECK(FakeAnswer(false, u"Discard me"));
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndUserCanceled);
    CHECK_EQ(Buffer(), u"Keep me");
    CHECK(GuardFrom(8));

    Prefill(u"x");
    CHECK_EQ(Open(96), 0);
    CHECK(FakeAnswer(true, u"First"));
    CHECK(FakeAnswer(true, u"Second"));
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"First");

    Prefill(u"Original");
    CHECK_EQ(Open(96), 0);
    const uint64_t id = FakeShownId();
    CHECK_EQ(sceImeDialogAbort(), 0);
    CHECK(FakeClosed(id));
    CHECK(FakeAnswer(true, u"Too late"));  // an answer racing the abort is dropped
    CHECK_EQ(GameFinish(), kEndAborted);
    CHECK_EQ(Buffer(), u"Original");
    SetKeyboardForTest(nullptr);
}

void TestKeyboardScripts() {
    std::puts("keyboard: scripts answer in place of the player and take the keyboard down");
    Reset();
    UseFakeKeyboard(true);
    Prefill(u"Hello");
    ScriptConfirm("Scripted on the keyboard");
    CHECK_EQ(Open(96), 0);
    const uint64_t first = FakeShownId();
    CHECK_EQ(sceImeDialogGetStatus(), kStatusRunning);  // a script waits until the dialog has been up a moment
    CHECK(PollUntilFinished());
    CHECK(FakeClosed(first));
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"Scripted on the keyboard");

    ScriptCancel();
    CHECK_EQ(Open(96), 0);
    const uint64_t second = FakeShownId();
    CHECK(PollUntilFinished());
    CHECK(FakeClosed(second));
    CHECK_EQ(GameFinish(), kEndUserCanceled);
    CHECK_EQ(Buffer(), u"Scripted on the keyboard");

    Prefill(u"");
    input::remote::Parse("ime 0123456789ABC");
    CHECK_EQ(Open(8), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"01234567");
    CHECK(GuardFrom(9));

    // The player answered before the script ran: the script waits for the next dialog.
    Prefill(u"x");
    CHECK_EQ(Open(96), 0);
    CHECK(FakeAnswer(true, u"Player"));
    ScriptConfirm("For the next one");
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"Player");
    CHECK_EQ(Open(96), 0);
    CHECK(PollUntilFinished());
    CHECK_EQ(GameFinish(), kEndOk);
    CHECK_EQ(Buffer(), u"For the next one");
    SetKeyboardForTest(nullptr);
}

void TestKeyboardUnavailable() {
    std::puts("keyboard unavailable (no game window): the window opens instead");
    Reset();
    UseFakeKeyboard(false);
    Prefill(u"Hello");
    CHECK_EQ(Open(96), 0);
    CHECK(WaitDialog() != nullptr);
    CHECK_EQ(FakeShows(), 0);
    CHECK_EQ(sceImeDialogAbort(), 0);
    CHECK_EQ(GameFinish(), kEndAborted);
    SetKeyboardForTest(nullptr);
}

// After a dialog closes the game's pad stays neutral while anything that answers or types
// in one is still held: every button (the keyboard closes with R2, which grabs in LBP3, and
// types with Square, Triangle, L1, R1, L2, L3), plus Enter, Esc and Backspace.
void TestGateEveryButton() {
    std::puts("gate: every button, Enter, Esc and Backspace hold it after a dialog closes");
    Reset();
    // The keys are read from this PC's keyboard: wait until none is down.
    WaitFor([] { return !((GetAsyncKeyState(VK_RETURN) | GetAsyncKeyState(VK_ESCAPE) | GetAsyncKeyState(VK_BACK)) & 0x8000); });
    for (uint32_t bit : {ps4::PAD_L3, ps4::PAD_R3, ps4::PAD_OPTIONS, ps4::PAD_UP, ps4::PAD_RIGHT, ps4::PAD_DOWN,
                         ps4::PAD_LEFT, ps4::PAD_L2, ps4::PAD_R2, ps4::PAD_L1, ps4::PAD_R1, ps4::PAD_TRIANGLE,
                         ps4::PAD_CIRCLE, ps4::PAD_CROSS, ps4::PAD_SQUARE, ps4::PAD_TOUCH_PAD})
        CHECK(hle::modal::AnswerHeld(bit));
    CHECK(!hle::modal::AnswerHeld(0));
    hle::modal::Begin("imetest");
    hle::modal::End();
    CHECK(hle::modal::HoldsPad(ps4::PAD_R2));
    CHECK(hle::modal::HoldsPad(ps4::PAD_SQUARE | ps4::PAD_L1));
    CHECK(!hle::modal::HoldsPad(0));
    CHECK(!hle::modal::HoldsPad(ps4::PAD_R2));  // released once: a new R2 is the game's
}

// imetest --pad cross|circle. LBP3_AUTOPRESS taps the button for 150 ms every 2 s
// through input::Read, as in an automated game run; the tap that's held when the dialog
// opens doesn't count, the next one confirms or cancels. Then the modal gate must stay
// up while the button is still held, and come down after.
// imetest --pad-keyboard cross|circle: the same with the (fake) keyboard up. The keyboard
// reads the pad itself, but under LBP3_AUTOPRESS the dialog answers the taps as the window
// did (a tapped Cross would otherwise type a key every 2 s and never close it): Cross
// confirms the text unchanged, Circle cancels, and the keyboard is taken down.
int PadCase(const std::string& button, bool keyboard) {
    using std::chrono::steady_clock;
    const bool cross = button == "cross";
    std::printf("pad (child%s): %s %s, the gate stays up while it's held\n", keyboard ? ", keyboard" : "",
                button.c_str(), cross ? "confirms" : "cancels");
    // input::Init reads user\config\input.ini under the working folder and writes a
    // default one if there's none: give it a folder of its own, removed at the end.
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"lbp3-imetest-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    SetCurrentDirectoryW(dir.c_str());
    _putenv_s("LBP3_AUTOPRESS", button.c_str());
    input::Init();
    SetCurrentDirectoryW(temp);
    DeleteFileW((dir + L"\\user\\config\\input.ini").c_str());
    RemoveDirectoryW((dir + L"\\user\\config").c_str());
    RemoveDirectoryW((dir + L"\\user").c_str());
    RemoveDirectoryW(dir.c_str());
    if (keyboard) UseFakeKeyboard(true);
    Prefill(u"Prefilled");
    CHECK_EQ(Open(96), 0);
    CHECK_EQ(FakeShows(), keyboard ? 1 : 0);
    CHECK(WaitFor([] { return sceImeDialogGetStatus() == kStatusFinished; }, 8000));
    if (keyboard) CHECK(FakeClosed(FakeShownId()));
    const auto finished = steady_clock::now();
    const uint32_t bit = cross ? ps4::PAD_CROSS : ps4::PAD_CIRCLE;
    ps4::PadData pad{};
    input::Read(0, pad);
    CHECK((pad.buttons & bit) != 0);  // the closing tap is still held...
    CHECK(hle::modal::Active());      // ...so the game still sees a neutral pad
    bool leaked = false;
    while (hle::modal::Active() && steady_clock::now() - finished < std::chrono::seconds(3)) {
        sceImeDialogGetStatus();  // the game's poll, which lets the gate down
        input::Read(0, pad);
        leaked |= (pad.buttons & bit) && !hle::modal::Active();
        Sleep(5);
    }
    const auto down = steady_clock::now() - finished;
    CHECK(!leaked);
    CHECK(!hle::modal::Active());
    CHECK(down < std::chrono::milliseconds(1500));
    Result r;
    std::memset(&r, 0, sizeof(r));
    r.endStatus = -1;
    CHECK_EQ(sceImeDialogGetResult(&r), 0);
    CHECK_EQ(r.endStatus, cross ? kEndOk : kEndUserCanceled);
    CHECK_EQ(sceImeDialogTerm(), 0);
    CHECK_EQ(Buffer(), u"Prefilled");
    std::printf("pad (child): %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

// imetest --ime-window: LBP3_IME=window (read once, so set before the first dialog) keeps
// the native window though a keyboard is available.
int ForcedWindowCase() {
    std::puts("LBP3_IME=window (child): the window, though a keyboard is available");
    _putenv_s("LBP3_IME", "window");
    UseFakeKeyboard(true);
    Prefill(u"Hello");
    CHECK_EQ(Open(96), 0);
    CHECK(WaitDialog() != nullptr);
    CHECK_EQ(FakeShows(), 0);
    CHECK_EQ(sceImeDialogAbort(), 0);
    CHECK_EQ(GameFinish(), kEndAborted);
    std::printf("LBP3_IME=window (child): %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

// Runs `imetest <args>` with our output handles; returns its exit code.
int RunChild(const wchar_t* args) {
    std::fflush(stdout);
    std::fflush(stderr);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    for (HANDLE h : {si.hStdOutput, si.hStdError}) SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) return -1;
    if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 2);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

// One dialog for a person to try; prints what the game would get.
int Manual() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Prefill(u"Hello");
    if (Open(96) != 0) return 1;
    while (sceImeDialogGetStatus() != kStatusFinished) Sleep(16);
    Result r{};
    sceImeDialogGetResult(&r);
    sceImeDialogTerm();
    std::printf("end status %d, buffer \"%s\"\n", r.endStatus, Utf8(Buffer()).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--manual") return Manual();
    if (argc > 2 && std::string(argv[1]) == "--pad") return PadCase(argv[2], false);
    if (argc > 2 && std::string(argv[1]) == "--pad-keyboard") return PadCase(argv[2], true);
    if (argc > 1 && std::string(argv[1]) == "--ime-window") return ForcedWindowCase();
    TestIdle();
    TestBadParams();
    TestMaxLengthClamped();
    TestScriptedConfirm();
    TestWindowConfirm();
    TestKeysAndCancel();
    TestScriptedCancel();
    TestAbort();
    TestMaxLength();
    TestUnicode();
    TestRemoteCommands();
    TestKeyboardRequest();
    TestKeyboardAnswers();
    TestKeyboardScripts();
    TestKeyboardUnavailable();
    TestGateEveryButton();
    TestIdle();
    CHECK_EQ(RunChild(L"--pad cross"), 0);
    CHECK_EQ(RunChild(L"--pad circle"), 0);
    CHECK_EQ(RunChild(L"--pad-keyboard cross"), 0);
    CHECK_EQ(RunChild(L"--pad-keyboard circle"), 0);
    CHECK_EQ(RunChild(L"--ime-window"), 0);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
