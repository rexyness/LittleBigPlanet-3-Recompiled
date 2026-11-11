// Native windows for the PS4 system dialogs; see hle/dialog_window.h.
#include "hle/dialog_window.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include "hle/pad_types.h"
#include "input/gamepad.h"

namespace hle::dialog_ui {

class Window {
public:
    explicit Window(Spec s) : spec(std::move(s)), openedMs(GetTickCount64()) {}

    const Spec spec;
    const uint64_t openedMs;
    std::atomic<HWND> hwnd{nullptr};
    std::atomic<bool> done{false};
    std::atomic<bool> closeRequested{false};
    Outcome outcome;  // written once, before `done`
    std::atomic<uint32_t> progress{0};

    // Pad state; UpdateStatus can be called from several game threads.
    std::mutex padMutex;
    uint32_t prevButtons = ~0u;  // so a button already held when the dialog opens doesn't count
    uint64_t lastPadMs = 0;
    bool autoAnswered = false;

    // The ImGui overlay (ui/dialog_overlay.h) instead of a window. Its answer arrives on the
    // presenter thread, the game's Close and LBP3_AUTOPRESS on game threads: `outcome` and
    // `done` are written under answerMutex, first writer wins.
    bool overlay = false;               // set before Show returns, then read-only
    std::atomic<uint64_t> overlayId{0};
    std::mutex answerMutex;
};

namespace {

enum : int { kIdButton1 = 1001, kIdButton2 = 1002, kIdText = 1100, kIdIcon = 1101, kIdList = 1102, kIdProgress = 1103 };
enum : UINT {
    kMsgPress = WM_APP + 1,  // wParam: button (1, 2, kDismiss) or 0 = the focused one
    kMsgMove,                // wParam: list selection delta (signed)
    kMsgFocus,               // wParam: 1 = previous control, 0 = next
    kMsgProgress,            // wParam: 0..100
    kMsgClose,               // the game closes it
};

constexpr uint64_t kPadIntervalMs = 10;  // UpdateStatus is polled in tight loops; read the pad at most this often
std::atomic<uint32_t> g_autoAnswerMs{1000};
std::atomic<PadReader> g_padReader{nullptr};
std::atomic<int> g_autoPressMode{-1};

bool AutoPressOn() {
    const int mode = g_autoPressMode.load();
    if (mode >= 0) return mode == 1;
    static const bool env = std::getenv("LBP3_AUTOPRESS") != nullptr;
    return env;
}

// ---- the overlay front end

std::atomic<const OverlayForTest*> g_overlayHooks{nullptr};
std::atomic<int> g_frontEndMode{-1};

bool OverlayAvailable() {
    const OverlayForTest* h = g_overlayHooks.load();
    return h ? h->available() : ui::dialog::Available();
}
uint64_t OverlayShow(ui::dialog::Request r, ui::dialog::AnswerFn fn) {
    const OverlayForTest* h = g_overlayHooks.load();
    return h ? h->show(std::move(r), std::move(fn)) : ui::dialog::Show(std::move(r), std::move(fn));
}
void OverlayProgress(uint64_t id, uint32_t percent) {
    const OverlayForTest* h = g_overlayHooks.load();
    h ? h->setProgress(id, percent) : ui::dialog::SetProgress(id, percent);
}
void OverlayClose(uint64_t id) {
    const OverlayForTest* h = g_overlayHooks.load();
    h ? h->close(id) : ui::dialog::Close(id);
}

// LBP3_DIALOGS=window keeps the native windows even with a game window up.
bool WindowsForced() {
    const int mode = g_frontEndMode.load();
    if (mode >= 0) return mode == 1;
    static const bool env = [] {
        const char* v = std::getenv("LBP3_DIALOGS");
        return v && std::strcmp(v, "window") == 0;
    }();
    return env;
}

ui::dialog::Request ToRequest(const Spec& s, bool padIgnored) {
    ui::dialog::Request r;
    switch (s.kind) {
    case Kind::Message: r.kind = ui::dialog::Kind::Message; break;
    case Kind::List: r.kind = ui::dialog::Kind::List; break;
    case Kind::Progress: r.kind = ui::dialog::Kind::Progress; break;
    }
    switch (s.icon) {
    case Icon::None: r.icon = ui::dialog::Icon::None; break;
    case Icon::Info: r.icon = ui::dialog::Icon::Info; break;
    case Icon::Question: r.icon = ui::dialog::Icon::Question; break;
    case Icon::Warning: r.icon = ui::dialog::Icon::Warning; break;
    case Icon::Error: r.icon = ui::dialog::Icon::Error; break;
    }
    r.text = s.text;
    r.button1 = s.button1;
    r.button2 = s.button2;
    r.defaultButton = s.defaultButton;
    r.cancelButton = s.cancelButton;  // kDismiss is -1 in both
    for (size_t i = 0; i < s.items.size(); ++i)
        r.items.push_back({s.items[i], i < s.itemIcons.size() ? s.itemIcons[i] : std::wstring()});
    r.focus = s.focus;
    r.progress = 0;  // SetProgress follows
    r.padIgnored = padIgnored;
    r.logName = s.logName;
    return r;
}

// Overlay mode: the first answer stands (the player's, LBP3_AUTOPRESS's or the game's Close).
bool Record(Window& w, Outcome o) {
    std::lock_guard lock(w.answerMutex);
    if (w.done.load()) return false;
    w.outcome = o;
    w.done.store(true);
    return true;
}

bool ReadPad(uint32_t& buttons) {
    if (PadReader reader = g_padReader.load()) return reader(buttons);
    ps4::PadData pad{};
    const bool ok = input::Read(0, pad);
    buttons = ok ? pad.buttons : 0;
    return ok;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string ButtonName(const Spec& s, int button) {
    if (button == 1) return Narrow(s.button1);
    if (button == 2) return Narrow(s.button2);
    if (button == kDismiss) return "(cancel)";
    return "(closed)";
}

// In-memory DLGTEMPLATE. Positions are placeholders; WM_INITDIALOG lays the controls out.
class Template {
public:
    void Header(DWORD style, DWORD exStyle, WORD count, const wchar_t* title) {
        Dword(style);
        Dword(exStyle);
        buf_.insert(buf_.end(), {count, 0, 0, 100, 50, 0 /*menu*/, 0 /*class*/});
        Str(title);
        buf_.push_back(9);  // point size
        Str(L"Segoe UI");
    }
    void Item(DWORD style, WORD id, WORD atom, const wchar_t* className, const std::wstring& text) {
        if (buf_.size() % 2) buf_.push_back(0);  // items are DWORD aligned
        Dword(style | WS_CHILD | WS_VISIBLE);
        Dword(0);
        buf_.insert(buf_.end(), {0, 0, 10, 10, id});
        if (atom) {
            buf_.insert(buf_.end(), {0xFFFF, atom});
        } else {
            Str(className);
        }
        Str(text.c_str());
        buf_.push_back(0);  // no creation data
    }
    const DLGTEMPLATE* Get() const { return reinterpret_cast<const DLGTEMPLATE*>(buf_.data()); }

private:
    void Dword(DWORD v) { buf_.insert(buf_.end(), {LOWORD(v), HIWORD(v)}); }
    void Str(const wchar_t* s) {
        for (; *s; ++s) buf_.push_back(static_cast<WORD>(*s));
        buf_.push_back(0);
    }
    std::vector<WORD> buf_;
};

constexpr WORD kAtomButton = 0x0080, kAtomStatic = 0x0082, kAtomListBox = 0x0083;

DWORD DialogStyle(const Spec& s) {
    DWORD style = DS_MODALFRAME | DS_SETFONT | DS_NOIDLEMSG | WS_POPUP | WS_CAPTION;
    if (s.cancelButton) style |= WS_SYSMENU;  // a close box only where closing means something
    return style;
}
DWORD DialogExStyle(const Spec& s) {
    DWORD ex = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    if (s.kind == Kind::Progress) ex |= WS_EX_NOACTIVATE;  // don't take the keyboard away from the game
    return ex;
}

Template Build(const Spec& s) {
    const bool icon = s.icon != Icon::None;
    const WORD count = static_cast<WORD>(1 + icon + (s.kind == Kind::List) + (s.kind == Kind::Progress) +
                                         !s.button1.empty() + !s.button2.empty());
    Template t;
    t.Header(DialogStyle(s), DialogExStyle(s), count, L"LittleBigPlanet 3");
    if (icon) t.Item(SS_ICON, kIdIcon, kAtomStatic, nullptr, L"");
    t.Item(SS_LEFT | SS_NOPREFIX, kIdText, kAtomStatic, nullptr, s.text);
    if (s.kind == Kind::List)
        t.Item(WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, kIdList, kAtomListBox, nullptr, L"");
    if (s.kind == Kind::Progress) t.Item(0, kIdProgress, 0, PROGRESS_CLASSW, L"");
    if (!s.button1.empty())
        t.Item(WS_TABSTOP | (s.defaultButton == 1 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), kIdButton1, kAtomButton, nullptr,
               s.button1);
    if (!s.button2.empty())
        t.Item(WS_TABSTOP | (s.defaultButton == 2 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), kIdButton2, kAtomButton, nullptr,
               s.button2);
    return t;
}

// The game's window (SDL's), in this process only: another game may be running.
HWND FindGameWindow() {
    struct Ctx { DWORD pid; HWND found; } ctx{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(p);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != c->pid || !IsWindowVisible(h)) return TRUE;
            wchar_t cls[32] = {};
            GetClassNameW(h, cls, 32);
            if (std::wcscmp(cls, L"SDL_app") != 0) return TRUE;
            c->found = h;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

void PlaceCentred(HWND dlg, int width, int height) {
    RECT anchor{};
    const HWND game = FindGameWindow();
    if (!game || IsIconic(game) || !GetWindowRect(game, &anchor)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &anchor, 0);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& work = mi.rcWork;
    int x = (anchor.left + anchor.right - width) / 2;
    int y = (anchor.top + anchor.bottom - height) / 2;
    x = std::clamp(x, static_cast<int>(work.left), std::max<int>(work.left, work.right - width));
    y = std::clamp(y, static_cast<int>(work.top), std::max<int>(work.top, work.bottom - height));
    SetWindowPos(dlg, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
}

void Layout(HWND dlg, const Spec& s) {
    RECT du{7, 4, 240, 14};  // margin, gap, text width, button height (dialog units)
    MapDialogRect(dlg, &du);
    const int margin = du.left, gap = du.top, textW = du.right, btnH = du.bottom;
    RECT bw{50, 10, 0, 0};  // button width, progress bar height
    MapDialogRect(dlg, &bw);
    const int btnW = bw.left, progH = bw.top;

    const bool hasIcon = s.icon != Icon::None;
    const int iconSize = hasIcon ? GetSystemMetrics(SM_CXICON) : 0;
    const int textX = margin + (hasIcon ? iconSize + margin : 0);
    const int fullW = textX + textW - margin;

    HWND text = GetDlgItem(dlg, kIdText);
    RECT tr{0, 0, textW, 0};
    if (HDC dc = GetDC(text)) {
        HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0)));
        DrawTextW(dc, s.text.c_str(), -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        SelectObject(dc, old);
        ReleaseDC(text, dc);
    }
    const int textH = std::max<int>(tr.bottom, 1);
    int y = margin;
    if (hasIcon) SetWindowPos(GetDlgItem(dlg, kIdIcon), nullptr, margin, y, iconSize, iconSize, SWP_NOZORDER);
    SetWindowPos(text, nullptr, textX, y + std::max(0, (iconSize - textH) / 2), textW, textH, SWP_NOZORDER);
    y += std::max(iconSize, textH);

    if (HWND list = GetDlgItem(dlg, kIdList)) {
        const int itemH = static_cast<int>(SendMessageW(list, LB_GETITEMHEIGHT, 0, 0));
        const int rows = std::clamp(static_cast<int>(s.items.size()), 3, 8);
        const int listH = rows * itemH + 2 * GetSystemMetrics(SM_CYEDGE);
        y += 2 * gap;
        SetWindowPos(list, nullptr, margin, y, fullW, listH, SWP_NOZORDER);
        y += listH;
    }
    if (HWND bar = GetDlgItem(dlg, kIdProgress)) {
        y += 2 * gap;
        SetWindowPos(bar, nullptr, margin, y, fullW, progH, SWP_NOZORDER);
        y += progH;
    }
    HWND b1 = GetDlgItem(dlg, kIdButton1), b2 = GetDlgItem(dlg, kIdButton2);
    if (b1 || b2) {
        y += 2 * gap;
        int x = margin + fullW;
        if (b2) {
            x -= btnW;
            SetWindowPos(b2, nullptr, x, y, btnW, btnH, SWP_NOZORDER);
            x -= gap;
        }
        if (b1) SetWindowPos(b1, nullptr, x - btnW, y, btnW, btnH, SWP_NOZORDER);
        y += btnH;
    }
    y += margin;

    RECT wr{0, 0, margin + fullW + margin, y};
    AdjustWindowRectEx(&wr, DialogStyle(s), FALSE, DialogExStyle(s));
    PlaceCentred(dlg, wr.right - wr.left, wr.bottom - wr.top);
}

LPCWSTR IconResource(Icon i) {
    switch (i) {
    case Icon::Question: return MAKEINTRESOURCEW(32514);  // IDI_QUESTION
    case Icon::Warning: return MAKEINTRESOURCEW(32515);   // IDI_WARNING
    case Icon::Error: return MAKEINTRESOURCEW(32513);     // IDI_ERROR
    default: return MAKEINTRESOURCEW(32516);              // IDI_INFORMATION
    }
}

bool HasButton(const Spec& s, int b) { return (b == 1 && !s.button1.empty()) || (b == 2 && !s.button2.empty()); }

void Finish(HWND dlg, Window& w, int button, int item) {
    if (w.done.load()) return;
    w.outcome = {button, item};
    w.done.store(true);
    if (w.spec.kind == Kind::Progress) {
        DestroyWindow(dlg);
        PostQuitMessage(0);
    } else {
        EndDialog(dlg, 0);
    }
}

void Press(HWND dlg, Window& w, int button) {
    if (button != kDismiss && !HasButton(w.spec, button)) return;
    int item = -1;
    if (w.spec.kind == Kind::List && button == 1) {
        item = static_cast<int>(SendDlgItemMessageW(dlg, kIdList, LB_GETCURSEL, 0, 0));
        if (item < 0) return;  // nothing selected yet
    }
    Finish(dlg, w, button, item);
}

int FocusedButton(HWND dlg, const Spec& s) {
    const HWND f = GetFocus();
    if (f && f == GetDlgItem(dlg, kIdButton1)) return 1;
    if (f && f == GetDlgItem(dlg, kIdButton2)) return 2;
    if (s.kind == Kind::List) return 1;  // the list itself: choose the selected entry
    return s.defaultButton;
}

INT_PTR CALLBACK DialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* w = reinterpret_cast<Window*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        w = reinterpret_cast<Window*>(lp);
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        const Spec& s = w->spec;
        if (s.icon != Icon::None)
            SendDlgItemMessageW(dlg, kIdIcon, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(nullptr, IconResource(s.icon))), 0);
        if (HWND list = GetDlgItem(dlg, kIdList)) {
            for (const auto& item : s.items) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
            SendMessageW(list, LB_SETCURSEL, std::clamp<int>(s.focus, 0, std::max<int>(0, int(s.items.size()) - 1)), 0);
        }
        if (HWND bar = GetDlgItem(dlg, kIdProgress)) {
            SendMessageW(bar, PBM_SETRANGE32, 0, 100);
            SendMessageW(bar, PBM_SETPOS, w->progress.load(), 0);
        }
        Layout(dlg, s);
        if (HasButton(s, s.defaultButton)) SendMessageW(dlg, DM_SETDEFID, s.defaultButton == 1 ? kIdButton1 : kIdButton2, 0);
        w->hwnd.store(dlg);
        if (w->closeRequested.load()) {  // closed before the window existed
            Finish(dlg, *w, 0, -1);
            return TRUE;
        }
        if (s.kind == Kind::Progress) {
            ShowWindow(dlg, SW_SHOWNOACTIVATE);
            return FALSE;
        }
        SetForegroundWindow(dlg);
        HWND focus = GetDlgItem(dlg, kIdList);
        if (!focus) focus = GetDlgItem(dlg, s.defaultButton == 2 ? kIdButton2 : kIdButton1);
        if (focus) SetFocus(focus);
        return focus ? FALSE : TRUE;
    }
    case WM_COMMAND: {
        if (!w) break;
        const int id = LOWORD(wp), code = HIWORD(wp);
        if ((id == kIdButton1 || id == kIdButton2) && code == BN_CLICKED) {
            Press(dlg, *w, id == kIdButton1 ? 1 : 2);
            return TRUE;
        }
        if (id == kIdList && code == LBN_DBLCLK) {
            Press(dlg, *w, 1);
            return TRUE;
        }
        if (id == IDCANCEL) {  // Esc
            if (w->spec.cancelButton) Press(dlg, *w, w->spec.cancelButton);
            return TRUE;
        }
        if (id == IDOK) {  // Enter with no default button
            Press(dlg, *w, FocusedButton(dlg, w->spec));
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        if (w && w->spec.cancelButton) Press(dlg, *w, w->spec.cancelButton);
        return TRUE;
    case kMsgPress:
        if (w) Press(dlg, *w, wp ? static_cast<int>(static_cast<intptr_t>(wp)) : FocusedButton(dlg, w->spec));
        return TRUE;
    case kMsgMove:
        if (HWND list = GetDlgItem(dlg, kIdList)) {
            const int n = static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0));
            const int cur = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
            if (n > 0) SendMessageW(list, LB_SETCURSEL, std::clamp(cur + static_cast<int>(static_cast<intptr_t>(wp)), 0, n - 1), 0);
        }
        return TRUE;
    case kMsgFocus:
        SendMessageW(dlg, WM_NEXTDLGCTL, wp, FALSE);
        return TRUE;
    case kMsgProgress:
        SendDlgItemMessageW(dlg, kIdProgress, PBM_SETPOS, wp, 0);
        return TRUE;
    case kMsgClose:
        if (w) Finish(dlg, *w, 0, -1);
        return TRUE;
    case WM_DESTROY:
        if (w) w->hwnd.store(nullptr);
        break;
    }
    return FALSE;
}

void Run(std::shared_ptr<Window> w) {
    static const bool commonControls = [] {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
        return InitCommonControlsEx(&icc) != FALSE;
    }();
    (void)commonControls;
    const Template t = Build(w->spec);
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    const LPARAM self = reinterpret_cast<LPARAM>(w.get());
    if (w->spec.kind == Kind::Progress) {
        HWND dlg = CreateDialogIndirectParamW(inst, t.Get(), nullptr, DialogProc, self);
        MSG msg;
        while (dlg && !w->done.load() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(dlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        // The game often closes a progress window at once (a quick export): WM_INITDIALOG then
        // destroys it, and CreateDialog reports a failure (1400) that isn't one.
        if (!dlg && w->closeRequested.load()) {
            std::fprintf(stderr, "[dialog] %s: closed by the game before it appeared\n", w->spec.logName);
        } else if (!dlg) {
            std::fprintf(stderr, "[dialog] %s: couldn't create the window (%lu)\n", w->spec.logName, GetLastError());
        }
    } else if (DialogBoxIndirectParamW(inst, t.Get(), nullptr, DialogProc, self) == -1) {
        std::fprintf(stderr, "[dialog] %s: couldn't create the window (%lu)\n", w->spec.logName, GetLastError());
    }
    if (!w->done.load()) {  // the window failed: don't leave the game waiting
        w->outcome = {w->spec.cancelButton ? w->spec.cancelButton : 0, -1};
        w->done.store(true);
    }
}

}  // namespace

std::shared_ptr<Window> Show(Spec spec) {
    auto w = std::make_shared<Window>(std::move(spec));
    if (!WindowsForced() && OverlayAvailable()) {
        w->overlay = true;
        std::fprintf(stderr, "[dialog] %s: on the overlay\n", w->spec.logName);
        // The answer comes on the presenter thread, possibly after the game is done with the
        // dialog (common_dialog drops its Window on Close): hence the weak pointer.
        std::weak_ptr<Window> weak = w;
        const uint64_t id = OverlayShow(ToRequest(w->spec, AutoPressOn()), [weak](const ui::dialog::Answer& a) {
            if (auto self = weak.lock()) Record(*self, {a.button, a.item});
        });
        w->overlayId.store(id);
        if (!id) {  // as a window that failed: don't leave the game waiting
            std::fprintf(stderr, "[dialog] %s: the overlay didn't take the dialog\n", w->spec.logName);
            Record(*w, {w->spec.cancelButton ? w->spec.cancelButton : 0, -1});
        }
        return w;
    }
    std::fprintf(stderr, "[dialog] %s: on a window\n", w->spec.logName);
    std::thread(Run, w).detach();
    return w;
}

bool Poll(Window& w, Outcome& out) {
    if (w.overlay) {
        // The overlay reads the pad itself; only LBP3_AUTOPRESS is answered here.
        if (!w.done.load() && AutoPressOn() && w.spec.autoAnswer) {
            std::lock_guard lock(w.padMutex);
            const int b = w.spec.autoAnswer;
            const uint64_t id = w.overlayId.load();
            if (!w.autoAnswered && id && GetTickCount64() - w.openedMs >= g_autoAnswerMs.load() &&
                (b == kDismiss || HasButton(w.spec, b))) {
                w.autoAnswered = true;
                std::fprintf(stderr, "[dialog] %s: LBP3_AUTOPRESS answers \"%s\"\n", w.spec.logName,
                             ButtonName(w.spec, b).c_str());
                int item = -1;  // as the window: button 1 of a list takes the selected (first-focused) entry
                if (w.spec.kind == Kind::List && b == 1 && !w.spec.items.empty())
                    item = std::clamp<int>(w.spec.focus, 0, static_cast<int>(w.spec.items.size()) - 1);
                if (Record(w, {b, item})) OverlayClose(id);
            }
        }
        if (!w.done.load()) return false;
        std::lock_guard lock(w.answerMutex);
        out = w.outcome;
        return true;
    }
    if (w.done.load()) {
        out = w.outcome;
        return true;
    }
    if (!HasButton(w.spec, 1) && !HasButton(w.spec, 2) && !w.spec.cancelButton) return false;  // nothing to press
    const HWND h = w.hwnd.load();
    const uint64_t now = GetTickCount64();
    std::lock_guard lock(w.padMutex);
    if (AutoPressOn()) {  // the pad's presses are synthetic: answer with the safe choice instead
        if (w.spec.autoAnswer && h && !w.autoAnswered && now - w.openedMs >= g_autoAnswerMs.load()) {
            w.autoAnswered = true;
            std::fprintf(stderr, "[dialog] %s: LBP3_AUTOPRESS answers \"%s\"\n", w.spec.logName,
                         ButtonName(w.spec, w.spec.autoAnswer).c_str());
            PostMessageW(h, kMsgPress, static_cast<WPARAM>(static_cast<intptr_t>(w.spec.autoAnswer)), 0);
        }
        return false;
    }
    if (now - w.lastPadMs < kPadIntervalMs) return false;
    w.lastPadMs = now;
    uint32_t buttons = 0;
    if (!ReadPad(buttons)) buttons = 0;
    const uint32_t pressed = buttons & ~w.prevButtons;
    w.prevButtons = buttons;
    if (!h || !pressed) return false;
    const bool list = w.spec.kind == Kind::List;
    if (pressed & ps4::PAD_CROSS) {
        PostMessageW(h, kMsgPress, 0, 0);
    } else if (pressed & ps4::PAD_CIRCLE) {
        if (w.spec.cancelButton) PostMessageW(h, kMsgPress, static_cast<WPARAM>(static_cast<intptr_t>(w.spec.cancelButton)), 0);
    } else if (list && (pressed & (ps4::PAD_UP | ps4::PAD_DOWN))) {
        PostMessageW(h, kMsgMove, static_cast<WPARAM>(static_cast<intptr_t>((pressed & ps4::PAD_UP) ? -1 : 1)), 0);
    } else if (!list && (pressed & (ps4::PAD_LEFT | ps4::PAD_RIGHT | ps4::PAD_UP | ps4::PAD_DOWN))) {
        PostMessageW(h, kMsgFocus, (pressed & (ps4::PAD_LEFT | ps4::PAD_UP)) ? 1 : 0, 0);
    }
    return false;
}

void SetProgress(Window& w, uint32_t percent) {
    w.progress.store(std::min<uint32_t>(percent, 100));
    if (w.overlay) {
        if (const uint64_t id = w.overlayId.load()) OverlayProgress(id, w.progress.load());
        return;
    }
    if (HWND h = w.hwnd.load()) PostMessageW(h, kMsgProgress, w.progress.load(), 0);
}

void Close(Window& w) {
    w.closeRequested.store(true);
    if (w.overlay) {
        Record(w, {0, -1});  // closed by the game, unless already answered
        if (const uint64_t id = w.overlayId.load()) OverlayClose(id);
        return;
    }
    if (HWND h = w.hwnd.load()) PostMessageW(h, kMsgClose, 0, 0);
}

void* NativeHandle(const Window& w) { return w.hwnd.load(); }
bool OnOverlay(const Window& w) { return w.overlay; }
void SetPadReaderForTest(PadReader reader) { g_padReader.store(reader); }
void SetAutoPressForTest(int mode) { g_autoPressMode.store(mode); }
void SetAutoAnswerDelayForTest(uint32_t ms) { g_autoAnswerMs.store(ms); }
void SetOverlayForTest(const OverlayForTest* overlay) { g_overlayHooks.store(overlay); }
void SetFrontEndForTest(int mode) { g_frontEndMode.store(mode); }

}  // namespace hle::dialog_ui
