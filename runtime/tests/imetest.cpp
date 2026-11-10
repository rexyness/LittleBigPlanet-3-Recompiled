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
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>

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
    CHECK_EQ(Open(kMaxTextLength + 1), kErrorInvalidMaxTextLength);
    CHECK_EQ(sceImeDialogGetStatus(), kStatusNone);
    CHECK(!hle::modal::Active());
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

// imetest --pad cross|circle. LBP3_AUTOPRESS taps the button for 150 ms every 2 s
// through input::Read, as in an automated game run; the tap that's held when the dialog
// opens doesn't count, the next one confirms or cancels. Then the modal gate must stay
// up while the button is still held, and come down after.
int PadCase(const std::string& button) {
    using std::chrono::steady_clock;
    const bool cross = button == "cross";
    std::printf("pad (child): %s %s, the gate stays up while it's held\n", button.c_str(), cross ? "confirms" : "cancels");
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
    Prefill(u"Prefilled");
    CHECK_EQ(Open(96), 0);
    CHECK(WaitFor([] { return sceImeDialogGetStatus() == kStatusFinished; }, 8000));
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

// Runs `imetest --pad <button>` with our output handles; returns its exit code.
int RunPadChild(const wchar_t* button) {
    std::fflush(stdout);
    std::fflush(stderr);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --pad " + button;
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
    if (argc > 2 && std::string(argv[1]) == "--pad") return PadCase(argv[2]);
    TestIdle();
    TestBadParams();
    TestScriptedConfirm();
    TestWindowConfirm();
    TestKeysAndCancel();
    TestScriptedCancel();
    TestAbort();
    TestMaxLength();
    TestUnicode();
    TestRemoteCommands();
    TestIdle();
    CHECK_EQ(RunPadChild(L"cross"), 0);
    CHECK_EQ(RunPadChild(L"circle"), 0);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
