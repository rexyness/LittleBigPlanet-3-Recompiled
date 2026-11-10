// libSceImeDialog (text entry) and libSceIme (keyboard). Milestone 2, gap 12.
//
// How LittleBigPlanet 3 uses the dialog (eboot.bin; notes in m2/ime.md):
// - Every text field goes through one request queue, and one place opens the dialog
//   (+0x559060). The param is zeroed, then gets the user id, one language bit (the
//   system language), the max length (the caller's, else 512), the game's own UTF-16
//   buffer (0x201 units, prefilled with the current text), the centre of a 1920x1080
//   screen and a title. No options, text filter, placeholder or extended param.
//   Callers: text chat ("Enter Your Message", 96), the Popit in Create mode ("Speech",
//   "Note", "Item Name", "Sequencer Name"..., 256) and menu text fields (scripts'
//   Gooey DoTextEntry: level name and description, search, playlists, comments).
// - One poll (+0x559800), every frame: GetStatus; at FINISHED (2) GetResult, the buffer
//   is used only if the end status is OK, then Term. A failed Init means "cancelled".
// - Abort when the requester goes away; the next poll then sees FINISHED, calls
//   GetResult and Term.
//
// Here the dialog is a native window on its own thread, so the game keeps running.
// Each dialog is a Session shared with its window thread; the text goes into the
// game's buffer on a game thread (GetStatus/GetResult), never from the window thread.
#include "hle/ime_dialog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hle/kernel_errors.h"
#include "hle/modal.h"
#include "hle/register.h"
#include "input/gamepad.h"

namespace hle::ime_dialog {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// The longest the modal gate stays up after the dialog closes while Cross or Circle
// is still held (see GetStatus).
constexpr auto kGateReleaseTimeout = 1s;
// Scripted actions wait this long after the window opens, so it's seen in captures.
constexpr auto kScriptDelay = 150ms;

// One dialog. The window thread fills in the outcome; the game side reads it.
struct Session {
    // Set at Init, read-only afterwards.
    std::wstring title, placeholder, prefill;
    uint32_t maxLength = 0, type = 0, option = 0, enterLabel = 0;
    // Shared with the window thread, under `mutex`.
    std::mutex mutex;
    HWND window = nullptr;
    bool abort = false;   // the game aborted: close without a result
    bool closed = false;  // the window is gone (or never came up)
    int32_t end = kEndUserCanceled;
    std::wstring text;    // the confirmed text, already clamped to maxLength
};

// Game-side state. The game polls from one thread, but nothing here relies on that.
std::mutex g_mutex;
std::atomic<int32_t> g_status{kStatusNone};
std::atomic<bool> g_gateHeld{false};  // we hold hle::modal (one Begin, one End)
std::shared_ptr<Session> g_session;
char16_t* g_buffer = nullptr;
int32_t g_endStatus = kEndOk;
bool g_crossWasDown = true, g_circleWasDown = true;  // a fresh press confirms / cancels
Clock::time_point g_finishedAt;

struct Scripted {
    bool confirm;
    std::wstring text;
};
struct ScriptQueue {
    std::mutex mutex;
    std::deque<Scripted> queue;
};
ScriptQueue& Script() {
    static auto* q = new ScriptQueue;  // never destroyed: window threads may outlive main
    return *q;
}

std::wstring FromGame(const char16_t* s, size_t max) {
    std::wstring out;
    for (size_t i = 0; s && i < max && s[i]; ++i) out.push_back(static_cast<wchar_t>(s[i]));
    return out;
}

std::wstring StripBom(std::wstring s) {  // the game's localised strings start with U+FEFF
    if (!s.empty() && s.front() == 0xFEFF) s.erase(0, 1);
    return s;
}

// Cuts `text` to `max` UTF-16 units (the PS4 counts those) without splitting a pair.
void Clamp(std::wstring& text, uint32_t max) {
    if (text.size() <= max) return;
    size_t n = max;
    if (n > 0 && text[n - 1] >= 0xD800 && text[n - 1] <= 0xDBFF) --n;
    text.resize(n);
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// ---- The window (its own thread) ----

enum : int { kIdEdit = 100, kIdTitle = 101, kIdCount = 102 };
constexpr UINT kMsgAbort = WM_APP + 1;
constexpr UINT_PTR kScriptTimer = 1;
constexpr UINT kEmSetCueBanner = 0x1501;  // EM_SETCUEBANNER (commctrl.h)

struct Window {
    std::shared_ptr<Session> session;
    HWND edit = nullptr, count = nullptr;
    HFONT font = nullptr;
    Clock::time_point openedAt;
};

bool Multiline(const Session& s) { return (s.option & kOptionMultiline) != 0; }

std::wstring ToEdit(std::wstring text, bool multiline) {  // the edit control wants CR LF
    if (!multiline) return text;
    for (size_t i = 0; (i = text.find(L'\n', i)) != std::wstring::npos; i += 2) text.insert(i, 1, L'\r');
    return text;
}

std::wstring EditText(HWND edit, bool multiline) {
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(edit)) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(edit, text.data(), static_cast<int>(text.size()))));
    if (multiline) std::erase(text, L'\r');
    return text;
}

void UpdateCount(Window& w) {
    wchar_t buf[48];
    std::swprintf(buf, 48, L"%d / %u", GetWindowTextLengthW(w.edit), w.session->maxLength);
    SetWindowTextW(w.count, buf);
}

// The player is done with the dialog: record the outcome and close the window.
void Finish(HWND hwnd, Window& w, int32_t end) {
    Session& s = *w.session;
    std::wstring text;
    if (end == kEndOk) {
        text = EditText(w.edit, Multiline(s));
        Clamp(text, s.maxLength);  // EM_SETLIMITTEXT only limits typing, not pasted or set text
    }
    {
        std::lock_guard lock(s.mutex);
        if (!s.abort) {
            s.end = end;
            s.text = std::move(text);
        }
    }
    DestroyWindow(hwnd);
}

// Remote-control / test input: one queued action, once the window has been up a moment.
void RunScript(HWND hwnd, Window& w) {
    if (Clock::now() - w.openedAt < kScriptDelay) return;
    {
        std::lock_guard lock(w.session->mutex);
        if (w.session->abort) return;  // closing: the script is for the next dialog
    }
    std::optional<Scripted> action;
    {
        ScriptQueue& q = Script();
        std::lock_guard lock(q.mutex);
        if (q.queue.empty()) return;
        action = std::move(q.queue.front());
        q.queue.pop_front();
    }
    if (!action->confirm) {
        std::fprintf(stderr, "[ime] scripted: cancel\n");
        Finish(hwnd, w, kEndUserCanceled);
        return;
    }
    std::fprintf(stderr, "[ime] scripted: typing \"%s\" and confirming\n", Utf8(action->text).c_str());
    SetWindowTextW(w.edit, ToEdit(action->text, Multiline(*w.session)).c_str());
    Finish(hwnd, w, kEndOk);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* w = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!w) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) Finish(hwnd, *w, kEndOk);
        else if (LOWORD(wp) == IDCANCEL) Finish(hwnd, *w, kEndUserCanceled);
        else if (LOWORD(wp) == kIdEdit && HIWORD(wp) == EN_CHANGE) UpdateCount(*w);
        return 0;
    case DM_GETDEFID:  // Enter (via IsDialogMessage) presses OK
        return MAKELRESULT(IDOK, DC_HASDEFID);
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && w->edit) SetFocus(w->edit);
        return 0;
    case WM_TIMER:
        if (wp == kScriptTimer) RunScript(hwnd, *w);
        return 0;
    case WM_CLOSE:  // the title bar's close button
        Finish(hwnd, *w, kEndUserCanceled);
        return 0;
    case kMsgAbort:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kScriptTimer);
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        if (w->font) DeleteObject(w->font);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete w;
        return DefWindowProcW(hwnd, msg, wp, lp);
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void RegisterWindowClass() {
    static std::once_flag once;
    std::call_once(once, [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);
    });
}

// The game's window: the biggest visible top-level window of this process.
HWND FindGameWindow() {
    struct Best {
        HWND hwnd = nullptr;
        long long area = 0;
    } best;
    EnumWindows(
        [](HWND hwnd, LPARAM p) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
            wchar_t cls[64] = {};
            GetClassNameW(hwnd, cls, 64);
            // Not dialogs (ours, message boxes) or a console window lbp3.exe created.
            if (!std::wcscmp(cls, kWindowClass) || !std::wcscmp(cls, L"#32770") ||
                !std::wcscmp(cls, L"ConsoleWindowClass") || !std::wcscmp(cls, L"PseudoConsoleWindow"))
                return TRUE;
            RECT r;
            GetWindowRect(hwnd, &r);
            auto& b = *reinterpret_cast<Best*>(p);
            const long long area = static_cast<long long>(r.right - r.left) * (r.bottom - r.top);
            if (area > b.area) b = {hwnd, area};
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&best));
    return best.hwnd;
}

HWND Child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle, int id, HFONT font) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return h;
}

void RunWindow(std::shared_ptr<Session> s) {
    RegisterWindowClass();
    // Over the game: centred on it, topmost and in front, as the PS4's dialog is drawn
    // over the game. Without a game window (the test harness) it stays out of the way.
    const HWND game = FindGameWindow();
    RECT area;
    if (game) {
        GetWindowRect(game, &area);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    }
    const POINT centre{(area.left + area.right) / 2, (area.top + area.bottom) / 2};

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT | WS_EX_APPWINDOW | (game ? WS_EX_TOPMOST : 0);
    auto* w = new Window{s};
    HWND hwnd = CreateWindowExW(exStyle, kWindowClass, L"LittleBigPlanet 3", style, centre.x, centre.y, 1, 1, nullptr,
                                nullptr, GetModuleHandleW(nullptr), w);
    if (!hwnd) {
        std::fprintf(stderr, "[ime] couldn't create the dialog window (error %lu); reporting cancelled\n",
                     GetLastError());
        std::lock_guard lock(s->mutex);  // (`w` may already be freed by WM_NCDESTROY; leaked otherwise)
        s->closed = true;
        return;
    }

    // Layout in 96-dpi units, scaled to the monitor the window is on.
    const UINT dpi = GetDpiForWindow(hwnd);
    auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
    w->font = CreateFontIndirectW(&ncm.lfMessageFont);

    const bool multiline = Multiline(*s);
    const int width = 480, margin = 12, editTop = 34, editHeight = multiline ? 96 : 24;
    const int buttonWidth = 88, buttonHeight = 26, buttonTop = editTop + editHeight + 12;
    const int height = buttonTop + buttonHeight + margin;

    HWND title = Child(hwnd, L"STATIC", s->title.empty() ? L"Enter text" : s->title.c_str(), SS_NOPREFIX | SS_ENDELLIPSIS,
                       0, kIdTitle, w->font);
    DWORD editStyle = WS_TABSTOP | (multiline ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL : ES_AUTOHSCROLL);
    if (s->option & kOptionPassword) editStyle |= ES_PASSWORD;
    if (s->type == kTypeNumber) editStyle |= ES_NUMBER;
    w->edit = Child(hwnd, L"EDIT", ToEdit(s->prefill, multiline).c_str(), editStyle, WS_EX_CLIENTEDGE, kIdEdit, w->font);
    SendMessageW(w->edit, EM_SETLIMITTEXT, s->maxLength, 0);
    if (!s->placeholder.empty()) SendMessageW(w->edit, kEmSetCueBanner, TRUE, reinterpret_cast<LPARAM>(s->placeholder.c_str()));
    static const wchar_t* const kEnterLabels[] = {L"OK", L"Send", L"Search", L"Go"};  // SceImeEnterLabel
    HWND ok = Child(hwnd, L"BUTTON", kEnterLabels[s->enterLabel < 4 ? s->enterLabel : 0], BS_DEFPUSHBUTTON | WS_TABSTOP, 0,
                    IDOK, w->font);
    HWND cancel = Child(hwnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 0, IDCANCEL, w->font);
    w->count = Child(hwnd, L"STATIC", L"", SS_NOPREFIX, 0, kIdCount, w->font);
    UpdateCount(*w);

    MoveWindow(title, px(margin), px(12), px(width - 2 * margin), px(18), FALSE);
    MoveWindow(w->edit, px(margin), px(editTop), px(width - 2 * margin), px(editHeight), FALSE);
    MoveWindow(cancel, px(width - margin - buttonWidth), px(buttonTop), px(buttonWidth), px(buttonHeight), FALSE);
    MoveWindow(ok, px(width - margin - 2 * buttonWidth - 8), px(buttonTop), px(buttonWidth), px(buttonHeight), FALSE);
    MoveWindow(w->count, px(margin), px(buttonTop + 5), px(160), px(18), FALSE);

    RECT frame{0, 0, px(width), px(height)};
    AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, dpi);
    const int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(centre, MONITOR_DEFAULTTONEAREST), &mi);
    const int x = std::max<int>(mi.rcWork.left, std::min<int>(centre.x - fw / 2, mi.rcWork.right - fw));
    const int y = std::max<int>(mi.rcWork.top, std::min<int>(centre.y - fh / 2, mi.rcWork.bottom - fh));
    SetWindowPos(hwnd, nullptr, x, y, fw, fh, SWP_NOZORDER | SWP_NOACTIVATE);
    const int len = GetWindowTextLengthW(w->edit);
    SendMessageW(w->edit, EM_SETSEL, len, len);  // caret at the end, as on the PS4

    bool aborted;
    {
        std::lock_guard lock(s->mutex);
        aborted = s->abort;
        if (!aborted) s->window = hwnd;
    }
    if (aborted) {
        DestroyWindow(hwnd);
    } else {
        w->openedAt = Clock::now();
        SetTimer(hwnd, kScriptTimer, 50, nullptr);
        if (game) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        } else {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Enter and Esc close the dialog only on a fresh press: one still held from the
        // game window (Enter is the menus' Confirm key) arrives here as auto-repeat
        // (bit 30: the key was already down) and would confirm the prefill at once.
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE) && (msg.lParam & (1 << 30)))
            continue;
        if (IsDialogMessageW(hwnd, &msg)) continue;  // Enter = OK, Esc = Cancel, Tab
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    std::lock_guard lock(s->mutex);
    s->window = nullptr;
    s->closed = true;
}

// ---- Game side ----

void PostToWindow(UINT msg, WPARAM wp) {
    std::lock_guard lock(g_session->mutex);
    if (g_session->window) PostMessageW(g_session->window, msg, wp, 0);
}

// Under g_mutex. RUNNING -> FINISHED once the window has closed; the text goes into
// the game's buffer here, on a game thread.
void FinishIfClosed() {
    if (g_status != kStatusRunning || !g_session) return;
    int32_t end;
    std::wstring text;
    {
        std::lock_guard lock(g_session->mutex);
        if (!g_session->closed) return;
        end = g_session->end;
        text = g_session->text;
    }
    g_endStatus = end;
    if (end == kEndOk) {
        // `text` is at most maxLength units; the buffer holds maxLength + 1.
        for (size_t i = 0; i < text.size(); ++i) g_buffer[i] = static_cast<char16_t>(text[i]);
        g_buffer[text.size()] = 0;
        std::fprintf(stderr, "[ime] dialog confirmed: \"%s\" (%zu characters)\n", Utf8(text).c_str(), text.size());
    } else {
        std::fprintf(stderr, "[ime] dialog cancelled\n");
    }
    g_status = kStatusFinished;
    g_finishedAt = Clock::now();
}

}  // namespace

void ScriptConfirm(const std::string& utf8) {
    ScriptQueue& q = Script();
    std::lock_guard lock(q.mutex);
    q.queue.push_back({true, Wide(utf8)});
    std::fprintf(stderr, "[ime] scripted text queued: \"%s\"\n", utf8.c_str());
}

void ScriptCancel() {
    ScriptQueue& q = Script();
    std::lock_guard lock(q.mutex);
    q.queue.push_back({false, {}});
    std::fprintf(stderr, "[ime] scripted cancel queued\n");
}

int PS4_SYSV_ABI sceImeDialogInit(const Param* param, const void* extended) {
    std::lock_guard lock(g_mutex);
    int error = kernel::SCE_OK;
    if (!param) {
        error = kErrorInvalidAddress;
    } else if (g_status != kStatusNone) {
        error = kErrorBusy;
    } else if (!param->inputTextBuffer) {
        error = kErrorInvalidInputTextBuffer;
    } else if (param->maxTextLength == 0 || param->maxTextLength > kMaxTextLength) {
        error = kErrorInvalidMaxTextLength;
    }
    if (error != kernel::SCE_OK) {
        std::fprintf(stderr, "[ime] sceImeDialogInit refused: %08X\n", static_cast<uint32_t>(error));
        return error;
    }

    auto s = std::make_shared<Session>();
    s->maxLength = param->maxTextLength;
    s->type = param->type;
    s->option = param->option;
    s->enterLabel = param->enterLabel;
    s->title = StripBom(FromGame(param->title, 512));
    s->placeholder = StripBom(FromGame(param->placeholder, 512));
    s->prefill = FromGame(param->inputTextBuffer, param->maxTextLength);
    std::fprintf(stderr, "[ime] dialog opened: \"%s\", up to %u characters, text \"%s\" (type %u, options %#x)\n",
                 Utf8(s->title).c_str(), s->maxLength, Utf8(s->prefill).c_str(), s->type, s->option);
    // LBP3 passes neither. A filter is game code, which can't run on the window thread.
    if (param->filter) std::fprintf(stderr, "[ime] note: the game's text filter is not applied\n");
    if (extended) std::fprintf(stderr, "[ime] note: extended parameters (colours, keyboard filter) are ignored\n");

    g_session = s;
    g_buffer = param->inputTextBuffer;
    g_endStatus = kEndOk;
    g_crossWasDown = g_circleWasDown = true;  // the press that opened the dialog doesn't count
    if (!g_gateHeld) {
        modal::Begin("ime");
        g_gateHeld = true;
    }
    g_status = kStatusRunning;
    std::thread(RunWindow, s).detach();
    return kernel::SCE_OK;
}

// Polled every frame, dialog or not.
int PS4_SYSV_ABI sceImeDialogGetStatus() {
    if (g_status.load() == kStatusNone && !g_gateHeld.load()) return kStatusNone;
    std::lock_guard lock(g_mutex);
    ps4::PadData pad{};
    const uint32_t buttons = input::Read(0, pad) ? pad.buttons : 0;  // the real pad; the game sees a neutral one
    const bool cross = buttons & ps4::PAD_CROSS, circle = buttons & ps4::PAD_CIRCLE;
    if (g_status == kStatusRunning) {
        if (cross && !g_crossWasDown) {
            std::fprintf(stderr, "[ime] Cross on the pad: confirm\n");
            PostToWindow(WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED));
        } else if (circle && !g_circleWasDown) {
            std::fprintf(stderr, "[ime] Circle on the pad: cancel\n");
            PostToWindow(WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED));
        }
        g_crossWasDown = cross;
        g_circleWasDown = circle;
        FinishIfClosed();
    }
    // The game saw a neutral pad while the dialog was up; a Cross still held from
    // confirming would reach it as a fresh press (and reopen the text field), so the
    // gate stays up until Cross and Circle are released. Likewise Enter and Esc, which
    // are keyboard actions (Confirm, Back) once the game window has the focus again.
    const bool keyHeld = (GetAsyncKeyState(VK_RETURN) | GetAsyncKeyState(VK_ESCAPE)) & 0x8000;
    if (g_gateHeld && g_status != kStatusRunning &&
        ((!cross && !circle && !keyHeld) || Clock::now() - g_finishedAt > kGateReleaseTimeout)) {
        modal::End();
        g_gateHeld = false;
    }
    return g_status;
}

int PS4_SYSV_ABI sceImeDialogGetResult(Result* result) {
    std::lock_guard lock(g_mutex);
    if (g_status == kStatusNone) return kErrorDialogNotInUse;
    if (!result) return kErrorInvalidAddress;
    FinishIfClosed();
    if (g_status != kStatusFinished) return kErrorDialogNotFinished;
    result->endStatus = g_endStatus;
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceImeDialogAbort() {
    std::lock_guard lock(g_mutex);
    if (g_status == kStatusNone) return kErrorDialogNotInUse;
    FinishIfClosed();  // already closed by the player: that result stands
    if (g_status != kStatusRunning) return kErrorDialogNotRunning;
    {
        std::lock_guard sessionLock(g_session->mutex);
        g_session->abort = true;
        if (g_session->window) PostMessageW(g_session->window, kMsgAbort, 0, 0);
    }
    std::fprintf(stderr, "[ime] dialog aborted by the game\n");
    g_endStatus = kEndAborted;
    g_status = kStatusFinished;
    g_finishedAt = Clock::now();
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceImeDialogTerm() {
    std::lock_guard lock(g_mutex);
    if (g_status == kStatusNone) return kErrorDialogNotInUse;
    FinishIfClosed();
    if (g_status != kStatusFinished) return kErrorDialogNotFinished;
    g_session.reset();  // an aborted window still closing keeps its own reference
    g_buffer = nullptr;
    g_status = kStatusNone;
    return kernel::SCE_OK;
}

}  // namespace hle::ime_dialog

// libSceIme keyboard: the game opens the keyboard at boot (retrying while it gets
// 0x80BC0008, so that must never be returned) and calls sceImeUpdate every frame
// with an event handler, for USB keyboard typing (text chat). On PC the keyboard
// reaches the game as actions and the text dialog, so these succeed and deliver no
// events; registering them keeps the stub noise out of the log.
namespace hle::ime {

int PS4_SYSV_ABI sceImeKeyboardOpen(int32_t userId, const void* param) {
    (void)param;
    std::fprintf(stderr, "[ime] keyboard opened for user %d (no keyboard events are delivered)\n", userId);
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceImeKeyboardClose(int32_t userId) {
    (void)userId;
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceImeUpdate(void* handler) {
    (void)handler;
    return kernel::SCE_OK;
}

}  // namespace hle::ime

namespace hle {

void RegisterImeDialog(core::Linker& linker) {
    using namespace ime_dialog;
    using namespace ime;
    const char* d = "libSceImeDialog";
    HLE_REG(linker, d, sceImeDialogInit);
    HLE_REG(linker, d, sceImeDialogGetStatus);
    HLE_REG(linker, d, sceImeDialogGetResult);
    HLE_REG(linker, d, sceImeDialogAbort);
    HLE_REG(linker, d, sceImeDialogTerm);
    const char* k = "libSceIme";
    HLE_REG(linker, k, sceImeKeyboardOpen);
    HLE_REG(linker, k, sceImeKeyboardClose);
    HLE_REG(linker, k, sceImeUpdate);
}

}  // namespace hle
