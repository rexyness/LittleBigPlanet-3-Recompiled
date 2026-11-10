// Offline check of the system dialogs (hle/common_dialog.h): calls them with
// params filled the way LBP3's call sites fill them (addresses below are
// eboot.bin offsets), answers the windows with real window messages or a
// scripted pad, and checks the state machines, results and error codes.
// Run it from the build folder; it opens and closes a few dozen small windows
// by itself. Exit code 0 = everything passed.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#include "core/linker.h"
#include "core/nid.h"
#include "core/vfs.h"
#include "hle/common_dialog.h"
#include "hle/dialog_window.h"
#include "hle/modal.h"
#include "hle/pad_types.h"
#include "hle/register.h"

#include <SDL3/SDL.h>

using namespace hle::common_dialog;
namespace ui = hle::dialog_ui;

// hle/savedata.cpp's search, as the import flows call it (eboot.bin+0x4f48e0).
namespace hle::savedata {
struct DirName { char data[32]; };
struct DirNameSearchCond {
    int32_t userId;
    int32_t pad0;
    const char* titleId;
    const DirName* dirName;
    uint32_t key, order;
    uint8_t reserved[32];
};
struct DirNameSearchResult {
    uint32_t hitNum;
    int32_t pad0;
    DirName* dirNames;
    uint32_t dirNamesNum;
    uint32_t setNum;
    void* params;
    void* infos;
    uint8_t reserved[12];
    int32_t pad1;
};
int PS4_SYSV_ABI sceSaveDataDirNameSearch(const DirNameSearchCond* cond, DirNameSearchResult* result);
}  // namespace hle::savedata

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const char* what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("    FAIL (line %d): %s\n", line, what);
    }
}
void CheckEq(int64_t a, int64_t b, const char* what, int line) {
    ++g_checks;
    if (a != b) {
        ++g_failures;
        std::printf("    FAIL (line %d): %s: got %lld (0x%llX), want %lld (0x%llX)\n", line, what, static_cast<long long>(a),
                    static_cast<unsigned long long>(a), static_cast<long long>(b), static_cast<unsigned long long>(b));
    }
}
#define CHECK(c) Check((c), #c, __LINE__)
#define CHECK_EQ(a, b) CheckEq(static_cast<int64_t>(a), static_cast<int64_t>(b), #a, __LINE__)

void Section(const char* name) { std::printf("%s\n", name); }

// ---- scripted pad: tests set the buttons held; dialogs read them through the hook
std::atomic<uint32_t> g_pad{0};
bool TestPad(uint32_t& buttons) {
    buttons = g_pad.load();
    return true;
}

void Wait(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Polls `update` (like the game's loops) until it returns `want` or time runs out.
template <class F>
int32_t PollUntil(F update, int32_t want, int timeoutMs = 4000) {
    int32_t s = update();
    for (int t = 0; s != want && t < timeoutMs; t += 5) {
        Wait(5);
        s = update();
    }
    return s;
}

// Holds `buttons` on the pad for a while, then releases them, polling `update` throughout
// (from released, as the game polls from the moment it opens a dialog).
template <class F>
void Tap(F update, uint32_t buttons) {
    g_pad = 0;
    for (int i = 0; i < 4; ++i) { update(); Wait(5); }
    g_pad = buttons;
    for (int i = 0; i < 8; ++i) { update(); Wait(5); }
    g_pad = 0;
    for (int i = 0; i < 8; ++i) { update(); Wait(5); }
}

// ---- windows (this process only)
std::vector<HWND> OurDialogs() {
    struct Ctx { DWORD pid; std::vector<HWND> found; } ctx{GetCurrentProcessId(), {}};
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(p);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            wchar_t cls[16] = {};
            GetClassNameW(h, cls, 16);
            if (pid == c->pid && std::wcscmp(cls, L"#32770") == 0 && IsWindowVisible(h)) c->found.push_back(h);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// dialogtest --capture <folder>: saves each dialog as a .bmp, to look at the layout.
const char* g_captureDir = nullptr;
void Capture(HWND dlg) {
    static int n = 0;
    RECT r{};
    GetWindowRect(dlg, &r);
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(dlg, mem, PW_RENDERFULLCONTENT);
    BITMAPINFOHEADER bi{sizeof(bi), w, -h, 1, 32, BI_RGB};
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    GetDIBits(mem, bmp, 0, h, pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    char path[MAX_PATH];
    std::snprintf(path, MAX_PATH, "%s/dialog_%02d.bmp", g_captureDir, ++n);
    if (FILE* f = std::fopen(path, "wb")) {
        BITMAPFILEHEADER fh{0x4D42, static_cast<DWORD>(sizeof(fh) + sizeof(bi) + pixels.size()), 0, 0, sizeof(fh) + sizeof(bi)};
        std::fwrite(&fh, sizeof(fh), 1, f);
        std::fwrite(&bi, sizeof(bi), 1, f);
        std::fwrite(pixels.data(), 1, pixels.size(), f);
        std::fclose(f);
    }
}

HWND FindDialog(int timeoutMs = 4000) {
    for (int t = 0; t < timeoutMs; t += 10) {
        auto d = OurDialogs();
        if (d.size() == 1) {
            Wait(30);  // let WM_INITDIALOG finish laying it out
            if (g_captureDir) Capture(d[0]);
            return d[0];
        }
        Wait(10);
    }
    return nullptr;
}

bool WaitNoDialogs(int timeoutMs = 4000) {
    for (int t = 0; t < timeoutMs; t += 10) {
        if (OurDialogs().empty()) return true;
        Wait(10);
    }
    return false;
}

std::wstring Text(HWND dlg, int id = 1100) {
    wchar_t buf[2048] = {};
    GetDlgItemTextW(dlg, id, buf, 2048);
    return buf;
}
bool Contains(const std::wstring& s, const wchar_t* part) { return s.find(part) != std::wstring::npos; }

// A real click's message: WM_COMMAND BN_CLICKED from the button.
void Click(HWND dlg, int id) { PostMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(dlg, id))); }
void PressEsc(HWND dlg) { PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0); }  // what the dialog manager sends for Esc

constexpr int kButton1 = 1001, kButton2 = 1002, kList = 1102, kProgress = 1103;

// ---- params, filled like the game
template <class P>
void Base(P& p) {
    std::memset(&p, 0, sizeof(p));
    p.baseParam.size = sizeof(BaseParam);
    p.baseParam.magic = kBaseParamMagic + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&p.baseParam));
}

// The MsgDialog queue, eboot.bin+0x483cd: SYSTEM_MSG with the queued type and user.
struct MsgSystem {
    MsgDialogParam p;
    MsgSystemMessageParam sys;
    explicit MsgSystem(int32_t type) {
        Base(p);
        p.size = sizeof(MsgDialogParam);
        p.mode = kMsgModeSystem;
        std::memset(&sys, 0, sizeof(sys));
        sys.sysMsgType = type;
        p.sysMsgParam = &sys;
        p.userId = 1;
    }
};

struct MsgUser {
    MsgDialogParam p;
    MsgUserMessageParam user;
    MsgButtonsParam labels;
    MsgUser(int32_t buttons, const char* msg) {
        Base(p);
        p.size = sizeof(MsgDialogParam);
        p.mode = kMsgModeUser;
        std::memset(&user, 0, sizeof(user));
        std::memset(&labels, 0, sizeof(labels));
        user.buttonType = buttons;
        user.msg = msg;
        labels.msg1 = "Keep";
        labels.msg2 = "Replace";
        user.buttonsParam = &labels;
        p.userMsgParam = &user;
        p.userId = 1;
    }
};

struct SaveParam {
    SaveDataDialogParam p;
    SaveAnimationParam anim;
    SaveItems items;
    SaveNewItem newItem;
    SaveSystemMessageParam sys;
    SaveProgressBarParam bar;
    SaveDirName names[4];
    SaveParam() {
        Base(p);
        std::memset(&anim, 0, sizeof(anim));
        std::memset(&items, 0, sizeof(items));
        std::memset(&newItem, 0, sizeof(newItem));
        std::memset(&sys, 0, sizeof(sys));
        std::memset(&bar, 0, sizeof(bar));
        std::memset(names, 0, sizeof(names));
        p.size = sizeof(SaveDataDialogParam);
        p.animParam = &anim;
        p.items = &items;
        items.userId = 1;
    }
    void Name(int i, const char* n) { std::snprintf(names[i].data, sizeof(names[i].data), "%s", n); }
};

// eboot.bin+0x909ee0: LIST, animParam {1, 0}, focus on the latest data, optional new entry.
SaveParam* List(int32_t disp, int count, bool withNew) {
    auto* s = new SaveParam;
    s->p.mode = kSaveModeList;
    s->p.dispType = disp;
    s->anim.userOK = 1;
    s->Name(0, "LBPxPROFILExBACKUP00000001");
    s->Name(1, "LBPxPROFILExBACKUP00000002");
    s->Name(2, "LBPxPROFILExBACKUP00000003");
    s->items.dirName = s->names;
    s->items.dirNameNum = static_cast<uint32_t>(count);
    s->items.focusPos = kFocusDataLatest;
    if (withNew) {
        s->newItem.title = "LittleBigPlanet\xE2\x84\xA2 3 profile";
        s->items.newItem = &s->newItem;
    }
    return s;
}

// eboot.bin+0x90a3d0: SYSTEM_MSG for one saved data (overwrite, file corrupted).
SaveParam* System(int32_t disp, int32_t type, const char* dirName) {
    auto* s = new SaveParam;
    s->p.mode = kSaveModeSystem;
    s->p.dispType = disp;
    s->anim.userOK = 1;
    if (dirName) {
        s->Name(0, dirName);
        s->items.dirName = s->names;
        s->items.dirNameNum = 1;
    }
    s->sys.sysMsgType = type;
    s->p.sysMsgParam = &s->sys;
    return s;
}

// eboot.bin+0x90a150: PROGRESS_BAR, sysMsgType 1 (progress), no message.
SaveParam* Progress(int32_t disp, const char* dirName) {
    auto* s = new SaveParam;
    s->p.mode = kSaveModeProgressBar;
    s->p.dispType = disp;
    if (dirName) {
        s->Name(0, dirName);
        s->items.dirName = s->names;
        s->items.dirNameNum = 1;
    }
    s->bar.sysMsgType = 1;
    s->p.progressBarParam = &s->bar;
    return s;
}

struct Result {
    SaveDataDialogResult r;
    SaveDirName dirName;
    std::vector<uint8_t> param = std::vector<uint8_t>(0x530);
};
// As the save/load flows read it (eboot.bin+0x9d08a): dirName and param out-buffers.
int32_t GetResult(Result& out, bool withDirName = true) {
    std::memset(&out.r, 0, sizeof(out.r));
    std::memset(&out.dirName, 'X', sizeof(out.dirName));  // must come back as a string
    out.r.dirName = withDirName ? &out.dirName : nullptr;
    out.r.param = out.param.data();
    return sceSaveDataDialogGetResult(&out.r);
}
std::string DirName(const Result& out) { return std::string(out.dirName.data, strnlen(out.dirName.data, 32)); }

int32_t SaveUpdate() { return sceSaveDataDialogUpdateStatus(); }
int32_t MsgUpdate() { return sceMsgDialogUpdateStatus(); }

// ================================================================ tests

void TestInitialization() {
    Section("common dialog initialisation");
    CHECK_EQ(sceMsgDialogInitialize(), kErrorNotSystemInitialized);
    CHECK_EQ(sceSaveDataDialogInitialize(), kErrorNotSystemInitialized);
    CHECK_EQ(sceSaveDataDialogUpdateStatus(), kStatusNone);
    CHECK_EQ(sceMsgDialogUpdateStatus(), kStatusNone);
    // Boot: the save system first (eboot.bin+0x50859), then the NP setup (+0x337934), which
    // accepts only ALREADY_SYSTEM_INITIALIZED.
    CHECK_EQ(sceCommonDialogInitialize(), 0);
    CHECK_EQ(sceCommonDialogInitialize(), kErrorAlreadySystemInitialized);
}

void TestBusyAndNotInitialized() {
    Section("busy / not initialised");
    // eboot.bin+0x90c8b0 opens a NODATA message without initialising: must fail, not auto-initialise.
    std::unique_ptr<SaveParam> nodata(System(kSaveDispLoad, kSaveSysNoData, nullptr));
    CHECK_EQ(sceSaveDataDialogOpen(&nodata->p), kErrorNotInitialized);
    CHECK_EQ(sceSaveDataDialogUpdateStatus(), kStatusNone);

    CHECK_EQ(sceMsgDialogInitialize(), 0);
    CHECK_EQ(sceMsgDialogInitialize(), kErrorAlreadyInitialized);
    CHECK_EQ(sceMsgDialogUpdateStatus(), kStatusInitialized);
    CHECK_EQ(sceSaveDataDialogInitialize(), kErrorBusy);
    CHECK(sceCommonDialogIsUsed());
    CHECK_EQ(sceMsgDialogTerminate(), 0);
    CHECK_EQ(sceMsgDialogTerminate(), kErrorNotInitialized);
    CHECK(!sceCommonDialogIsUsed());

    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    CHECK_EQ(sceMsgDialogInitialize(), kErrorBusy);
    // The MsgDialog queue ignores Initialize's result; its Open then fails and it Terminates,
    // which must not release the save dialog's hold.
    MsgSystem m(kMsgSysPsnUgcRestriction);
    CHECK_EQ(sceMsgDialogOpen(&m.p), kErrorNotInitialized);
    CHECK_EQ(sceMsgDialogTerminate(), kErrorNotInitialized);
    CHECK_EQ(sceMsgDialogInitialize(), kErrorBusy);
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);
    CHECK(!hle::modal::Active());
}

void TestMsgDialogQueue() {
    Section("MsgDialog: queued system message (eboot.bin+0x48370)");
    MsgSystem m(kMsgSysPsnUgcRestriction);
    CHECK_EQ(sceMsgDialogOpen(nullptr), kErrorNotInitialized);
    sceMsgDialogInitialize();  // the queue ignores the result
    CHECK_EQ(sceMsgDialogOpen(nullptr), kErrorArgNull);
    MsgSystem bad(kMsgSysPsnUgcRestriction);
    bad.p.baseParam.magic ^= 1;
    CHECK_EQ(sceMsgDialogOpen(&bad.p), kErrorParamInvalid);
    MsgSystem badSize(kMsgSysPsnUgcRestriction);
    badSize.p.size = 0x80;
    CHECK_EQ(sceMsgDialogOpen(&badSize.p), kErrorParamInvalid);

    CHECK_EQ(sceMsgDialogOpen(&m.p), 0);
    CHECK_EQ(sceMsgDialogUpdateStatus(), kStatusRunning);
    CHECK(hle::modal::Active());
    CHECK_EQ(sceMsgDialogOpen(&m.p), kErrorBusy);
    MsgDialogResult r{};
    CHECK_EQ(sceMsgDialogGetResult(&r), kErrorNotFinished);
    HWND dlg = FindDialog();
    CHECK(dlg != nullptr);
    CHECK(Contains(Text(dlg), L"restricted"));
    CHECK(Contains(Text(dlg, kButton1), L"OK"));
    Click(dlg, kButton1);
    CHECK_EQ(PollUntil(MsgUpdate, kStatusFinished), kStatusFinished);
    CHECK(!hle::modal::Active());
    CHECK_EQ(sceMsgDialogGetResult(&r), 0);
    CHECK_EQ(r.mode, kMsgModeSystem);
    CHECK_EQ(r.result, kResultOk);
    CHECK_EQ(r.buttonId, kMsgButton1);
    // State 2: FINISHED -> Terminate; state 3 waits for NONE.
    CHECK_EQ(sceMsgDialogTerminate(), 0);
    CHECK_EQ(sceMsgDialogUpdateStatus(), kStatusNone);
    CHECK(WaitNoDialogs());

    Section("MsgDialog: empty store message, Esc");
    MsgSystem store(kMsgSysEmptyStore);
    sceMsgDialogInitialize();
    CHECK_EQ(sceMsgDialogOpen(&store.p), 0);
    dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"PlayStation Store"));
    PressEsc(dlg);
    CHECK_EQ(PollUntil(MsgUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(sceMsgDialogTerminate(), 0);
    CHECK(WaitNoDialogs());
    CHECK(!hle::modal::Active());
}

void TestMsgDialogUser() {
    Section("MsgDialog: user messages (not used by LBP3)");
    sceMsgDialogInitialize();
    MsgUser yesno(kMsgButtonsYesNo, "Replace the level?");
    CHECK_EQ(sceMsgDialogOpen(&yesno.p), 0);
    HWND dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"Replace the level?"));
    Click(dlg, kButton2);
    CHECK_EQ(PollUntil(MsgUpdate, kStatusFinished), kStatusFinished);
    MsgDialogResult r{};
    CHECK_EQ(sceMsgDialogGetResult(&r), 0);
    CHECK_EQ(r.result, kResultOk);
    CHECK_EQ(r.buttonId, kMsgButton2);
    CHECK(WaitNoDialogs());

    MsgUser okCancel(kMsgButtonsOkCancel, "Continue?");
    CHECK_EQ(sceMsgDialogOpen(&okCancel.p), 0);
    dlg = FindDialog();
    PressEsc(dlg);
    CHECK_EQ(PollUntil(MsgUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(sceMsgDialogGetResult(&r), 0);
    CHECK_EQ(r.result, kResultUserCanceled);
    CHECK_EQ(r.buttonId, kMsgButtonInvalid);
    CHECK(WaitNoDialogs());

    MsgUser two(kMsgButtonsTwoButtons, "Which one?");
    CHECK_EQ(sceMsgDialogOpen(&two.p), 0);
    dlg = FindDialog();
    CHECK(dlg && Text(dlg, kButton1) == L"Keep" && Text(dlg, kButton2) == L"Replace");
    Click(dlg, kButton1);
    CHECK_EQ(PollUntil(MsgUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(sceMsgDialogGetResult(&r), 0);
    CHECK_EQ(r.buttonId, kMsgButton1);
    CHECK(WaitNoDialogs());

    // Terminate while open closes the window and releases everything.
    MsgUser wait(kMsgButtonsYesNo, "Still there?");
    CHECK_EQ(sceMsgDialogOpen(&wait.p), 0);
    CHECK(FindDialog() != nullptr);
    CHECK_EQ(sceMsgDialogTerminate(), 0);
    CHECK_EQ(sceMsgDialogUpdateStatus(), kStatusNone);
    CHECK(WaitNoDialogs());
    CHECK(!hle::modal::Active());
    CHECK(!sceCommonDialogIsUsed());
}

// SlowSaveProfile / SaveLevel (eboot.bin+0x9c710, +0x90a570): list -> overwrite? -> progress -> close.
void TestSaveFlow() {
    Section("SaveDataDialog: export (save list, new entry)");
    CHECK_EQ(sceSaveDataDialogUpdateStatus(), kStatusNone);  // the flow initialises only from NONE
    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    std::unique_ptr<SaveParam> list(List(kSaveDispSave, 2, true));
    CHECK_EQ(sceSaveDataDialogOpen(&list->p), 0);
    CHECK_EQ(SaveUpdate(), kStatusRunning);
    CHECK(hle::modal::Active());
    HWND dlg = FindDialog();
    CHECK(dlg != nullptr);
    HWND lb = GetDlgItem(dlg, kList);
    CHECK_EQ(SendMessageW(lb, LB_GETCOUNT, 0, 0), 3);
    CHECK(Contains(Text(dlg, kButton1), L"Save"));
    SendMessageW(lb, LB_SETCURSEL, 0, 0);  // the new entry
    Click(dlg, kButton1);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK(!hle::modal::Active());
    Result res;
    CHECK_EQ(GetResult(res), 0);
    CHECK_EQ(res.r.mode, kSaveModeList);
    CHECK_EQ(res.r.result, kResultOk);
    CHECK(DirName(res).empty());  // the game then makes up a name ('%s%s%08X')
    CHECK(WaitNoDialogs());

    Section("SaveDataDialog: export onto existing data, overwrite answered No then Yes");
    CHECK_EQ(sceSaveDataDialogOpen(&list->p), 0);  // again from FINISHED
    dlg = FindDialog();
    SendMessageW(GetDlgItem(dlg, kList), LB_SETCURSEL, 2, 0);
    Click(dlg, kButton1);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res), 0);
    CHECK_EQ(res.r.result, kResultOk);
    CHECK(DirName(res) == "LBPxPROFILExBACKUP00000002");
    CHECK(WaitNoDialogs());

    std::unique_ptr<SaveParam> overwrite(System(kSaveDispSave, kSaveSysOverwrite, "LBPxPROFILExBACKUP00000002"));
    CHECK_EQ(sceSaveDataDialogOpen(&overwrite->p), 0);
    dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"overwrite") && Contains(Text(dlg), L"BACKUP00000002"));
    Tap(SaveUpdate, ps4::PAD_CROSS);  // Cross presses the focused button: No by default
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res, false), 0);  // eboot.bin+0x9d1a0 passes no dirName buffer
    CHECK(!(res.r.result == kResultOk && res.r.buttonId == kSaveButtonYes));  // the game proceeds only on this
    CHECK_EQ(res.r.result, kResultUserCanceled);
    CHECK(WaitNoDialogs());

    CHECK_EQ(sceSaveDataDialogOpen(&overwrite->p), 0);
    dlg = FindDialog();
    Click(dlg, kButton1);  // Yes
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res, false), 0);
    CHECK_EQ(res.r.result, kResultOk);
    CHECK_EQ(res.r.buttonId, kSaveButtonYes);
    CHECK(WaitNoDialogs());

    Section("SaveDataDialog: progress bar while saving, closed by the game");
    // State 7: not RUNNING -> open the progress bar (no Initialize: status is FINISHED).
    CHECK_EQ(SaveUpdate(), kStatusFinished);
    std::unique_ptr<SaveParam> bar(Progress(kSaveDispSave, "LBPxPROFILExBACKUP00000002"));
    CHECK_EQ(sceSaveDataDialogOpen(&bar->p), 0);
    CHECK_EQ(SaveUpdate(), kStatusRunning);
    CHECK_EQ(sceSaveDataDialogIsReadyToDisplay(), 1);
    dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"Saving"));
    CHECK_EQ(sceSaveDataDialogProgressBarSetValue(0, 40), 0);
    CHECK_EQ(sceSaveDataDialogProgressBarSetValue(1, 40), kErrorParamInvalid);
    Wait(50);
    CHECK_EQ(SendMessageW(GetDlgItem(dlg, kProgress), PBM_GETPOS, 0, 0), 40);
    Tap(SaveUpdate, ps4::PAD_CROSS);  // nothing to press on a progress bar
    Tap(SaveUpdate, ps4::PAD_CIRCLE);
    CHECK_EQ(SaveUpdate(), kStatusRunning);
    CHECK(hle::modal::Active());
    SaveDataDialogCloseParam close{};
    CHECK_EQ(sceSaveDataDialogClose(&close), 0);
    CHECK_EQ(sceSaveDataDialogGetStatus(), kStatusFinished);  // at once: some flows spin on it
    CHECK(!hle::modal::Active());
    CHECK_EQ(sceSaveDataDialogClose(&close), kErrorNotRunning);
    CHECK_EQ(sceSaveDataDialogProgressBarSetValue(0, 50), kErrorNotRunning);
    CHECK(WaitNoDialogs());
    // State 10: Close (ignored) and Terminate.
    CHECK(SaveUpdate() != kStatusNone);
    sceSaveDataDialogClose(&close);
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);
    CHECK_EQ(SaveUpdate(), kStatusNone);
    CHECK(!sceCommonDialogIsUsed());
}

// SlowLoadProfile / LoadLevel (eboot.bin+0x90c4d0, +0x97e490).
void TestLoadFlow() {
    Section("SaveDataDialog: import list cancelled (Esc, close box)");
    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    std::unique_ptr<SaveParam> list(List(kSaveDispLoad, 3, false));
    CHECK_EQ(sceSaveDataDialogOpen(&list->p), 0);
    HWND dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg, kButton1), L"Load"));
    CHECK_EQ(SendMessageW(GetDlgItem(dlg, kList), LB_GETCOUNT, 0, 0), 3);
    PressEsc(dlg);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    Result res;
    CHECK_EQ(GetResult(res), 0);  // the load flows check only this sign...
    CHECK_EQ(res.r.result, kResultUserCanceled);
    CHECK(DirName(res).empty());  // ...so a cancel must not name anything to mount
    CHECK(WaitNoDialogs());

    CHECK_EQ(sceSaveDataDialogOpen(&list->p), 0);
    dlg = FindDialog();
    PostMessageW(dlg, WM_CLOSE, 0, 0);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res), 0);
    CHECK_EQ(res.r.result, kResultUserCanceled);
    CHECK(DirName(res).empty());
    CHECK(WaitNoDialogs());

    Section("SaveDataDialog: import with the pad (D-pad down, Cross)");
    g_pad = ps4::PAD_CROSS;  // held from the menu that opened it: must not count
    CHECK_EQ(sceSaveDataDialogOpen(&list->p), 0);
    dlg = FindDialog();
    for (int i = 0; i < 20; ++i) { SaveUpdate(); Wait(5); }
    CHECK_EQ(SaveUpdate(), kStatusRunning);
    g_pad = 0;
    Tap(SaveUpdate, ps4::PAD_DOWN);
    Tap(SaveUpdate, ps4::PAD_CROSS);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res), 0);
    CHECK_EQ(res.r.result, kResultOk);
    CHECK(DirName(res) == "LBPxPROFILExBACKUP00000002");
    CHECK(WaitNoDialogs());

    Section("SaveDataDialog: import of corrupted data (progress, Close loop, FILE_CORRUPTED)");
    std::unique_ptr<SaveParam> bar(Progress(kSaveDispLoad, "LBPxPROFILExBACKUP00000002"));
    CHECK_EQ(sceSaveDataDialogOpen(&bar->p), 0);
    CHECK(FindDialog() != nullptr);
    SaveDataDialogCloseParam close{};
    int32_t s;
    do {  // eboot.bin+0x90ca80: Close, then spin until NONE or FINISHED
        sceSaveDataDialogClose(&close);
        s = SaveUpdate();
    } while (s != kStatusNone && s != kStatusFinished);
    CHECK_EQ(s, kStatusFinished);
    std::unique_ptr<SaveParam> corrupt(System(kSaveDispLoad, kSaveSysFileCorrupted, "LBPxPROFILExBACKUP00000002"));
    CHECK_EQ(sceSaveDataDialogOpen(&corrupt->p), 0);
    dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"corrupted"));
    Click(dlg, kButton1);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res), 0);
    CHECK_EQ(res.r.result, kResultUserCanceled);  // SDK 2.00: this message's OK reads as cancelled
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);
    CHECK(WaitNoDialogs());

    Section("SaveDataDialog: nothing to import (NODATA, eboot.bin+0x97e946), Circle");
    CHECK_EQ(SaveUpdate(), kStatusNone);
    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    std::unique_ptr<SaveParam> nodata(System(kSaveDispLoad, kSaveSysNoData, nullptr));
    nodata->anim.userOK = 0;  // eboot.bin+0x50560 leaves the animation on
    CHECK_EQ(sceSaveDataDialogOpen(&nodata->p), 0);
    dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"There is no saved data"));
    Tap(SaveUpdate, ps4::PAD_CIRCLE);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);
    CHECK(WaitNoDialogs());
    CHECK(!hle::modal::Active());
}

// The import lists with real save folders: the search finds only the exports, and the
// list opens on the newest one (focusPos DATALATEST).
void TestImportListOnDisk() {
    Section("SaveDataDialog: import list from save folders, newest selected");
    namespace fs = std::filesystem;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const fs::path root = fs::path(tmp) / ("lbp3_dialogtest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    core::vfs::Init({}, root);
    const fs::path saves = root / "savedata" / "CUSA00473";
    const char* folders[] = {"LBPxSAVE", "LBPxSAVExLITTLE", "LBPxSAVExPBACKUPx0000AAAA", "LBPxSAVExPBACKUPx0000BBBB",
                             "LBPxSAVExPBACKUPx0000CCCC", "LBPxSAVExLEVELx00000001"};
    for (const char* f : folders) fs::create_directories(saves / f, ec);
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(saves / "LBPxSAVExPBACKUPx0000AAAA", now - std::chrono::hours(48), ec);
    fs::last_write_time(saves / "LBPxSAVExPBACKUPx0000BBBB", now - std::chrono::hours(1), ec);
    fs::last_write_time(saves / "LBPxSAVExPBACKUPx0000CCCC", now - std::chrono::hours(24), ec);

    hle::savedata::DirName pattern{}, hits[8]{};
    std::snprintf(pattern.data, sizeof(pattern.data), "%%%s%%", "xPBACKUPx");  // the game's '%%%s%%'
    hle::savedata::DirNameSearchCond cond{};
    cond.userId = 1;
    cond.dirName = &pattern;
    hle::savedata::DirNameSearchResult found{};
    found.dirNames = hits;
    found.dirNamesNum = 8;
    CHECK_EQ(hle::savedata::sceSaveDataDirNameSearch(&cond, &found), 0);
    CHECK_EQ(found.hitNum, 3);  // not LBPxSAVE, LBPxSAVExLITTLE or the level export

    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    std::unique_ptr<SaveParam> list(List(kSaveDispLoad, 0, false));
    for (uint32_t i = 0; i < found.setNum && i < 4; ++i) list->names[i] = *reinterpret_cast<SaveDirName*>(&hits[i]);
    list->items.dirNameNum = found.setNum;
    CHECK_EQ(sceSaveDataDialogOpen(&list->p), 0);
    HWND dlg = FindDialog();
    HWND lb = dlg ? GetDlgItem(dlg, kList) : nullptr;
    CHECK_EQ(SendMessageW(lb, LB_GETCOUNT, 0, 0), 3);
    wchar_t row[256] = {};
    SendMessageW(lb, LB_GETTEXT, SendMessageW(lb, LB_GETCURSEL, 0, 0), reinterpret_cast<LPARAM>(row));
    CHECK(Contains(row, L"0000BBBB") && Contains(row, L"20"));  // the newest, with its date
    Tap(SaveUpdate, ps4::PAD_CROSS);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    Result res;
    CHECK_EQ(GetResult(res), 0);
    CHECK(DirName(res) == "LBPxSAVExPBACKUPx0000BBBB");
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);
    CHECK(WaitNoDialogs());
    fs::remove_all(root, ec);
}

// GameDataWrapper (eboot.bin+0x51500, +0x52220).
void TestBootSaveMessages() {
    Section("SaveDataDialog: boot save corrupted, backup restore (type 9), close box");
    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    std::unique_ptr<SaveParam> restore(System(kSaveDispLoad, kSaveSysCorruptedRestoreOld, "LBPxSAVE"));
    restore->anim.userOK = 0;
    CHECK_EQ(sceSaveDataDialogOpen(&restore->p), 0);
    HWND dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"corrupted") && Contains(Text(dlg), L"LBPxSAVE"));
    PostMessageW(dlg, WM_CLOSE, 0, 0);
    while (SaveUpdate() != kStatusFinished) Wait(10);  // the wrapper's usleep loop
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);
    CHECK(WaitNoDialogs());

    Section("SaveDataDialog: no space to save (state machine at eboot.bin+0x52220)");
    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    std::unique_ptr<SaveParam> nospace(System(kSaveDispSave, kSaveSysNoSpace, nullptr));
    nospace->anim.userOK = 0;
    nospace->sys.value = 3200;  // blocks of 32 KiB = 100 MB
    CHECK_EQ(sceSaveDataDialogOpen(&nospace->p), 0);
    dlg = FindDialog();
    CHECK(dlg && Contains(Text(dlg), L"100 MB"));
    Click(dlg, kButton1);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);
    CHECK(WaitNoDialogs());
    CHECK(!hle::modal::Active());
}

// fromEnv: the real switch, LBP3_AUTOPRESS in the environment (dialogtest runs itself for that).
void TestAutoPress(bool fromEnv) {
    Section(fromEnv ? "LBP3_AUTOPRESS from the environment: each dialog answers itself with the choice that changes nothing"
                    : "LBP3_AUTOPRESS: each dialog answers itself with the choice that changes nothing");
    if (!fromEnv) ui::SetAutoPressForTest(1);
    ui::SetAutoAnswerDelayForTest(150);
    Result res;

    CHECK_EQ(sceSaveDataDialogInitialize(), 0);
    // CONFIRM (save) has Yes focused; the synthetic Cross presses must not choose it.
    std::unique_ptr<SaveParam> confirm(System(kSaveDispSave, kSaveSysConfirm, "LBPxPROFILExBACKUP00000001"));
    CHECK_EQ(sceSaveDataDialogOpen(&confirm->p), 0);
    CHECK(FindDialog() != nullptr);
    for (int i = 0; i < 3; ++i) Tap(SaveUpdate, ps4::PAD_CROSS);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res, false), 0);
    CHECK_EQ(res.r.result, kResultUserCanceled);
    CHECK(WaitNoDialogs());

    std::unique_ptr<SaveParam> overwrite(System(kSaveDispSave, kSaveSysOverwrite, "LBPxPROFILExBACKUP00000001"));
    CHECK_EQ(sceSaveDataDialogOpen(&overwrite->p), 0);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res, false), 0);
    CHECK(!(res.r.result == kResultOk && res.r.buttonId == kSaveButtonYes));
    CHECK(WaitNoDialogs());

    std::unique_ptr<SaveParam> load(List(kSaveDispLoad, 3, false));
    CHECK_EQ(sceSaveDataDialogOpen(&load->p), 0);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res), 0);
    CHECK_EQ(res.r.result, kResultUserCanceled);
    CHECK(DirName(res).empty());
    CHECK(WaitNoDialogs());

    std::unique_ptr<SaveParam> save(List(kSaveDispSave, 3, true));
    CHECK_EQ(sceSaveDataDialogOpen(&save->p), 0);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(GetResult(res), 0);
    CHECK_EQ(res.r.result, kResultUserCanceled);
    CHECK(WaitNoDialogs());

    std::unique_ptr<SaveParam> nodata(System(kSaveDispLoad, kSaveSysNoData, nullptr));
    CHECK_EQ(sceSaveDataDialogOpen(&nodata->p), 0);
    CHECK_EQ(PollUntil(SaveUpdate, kStatusFinished), kStatusFinished);
    CHECK(WaitNoDialogs());

    // A progress bar is the game's to close.
    std::unique_ptr<SaveParam> bar(Progress(kSaveDispSave, nullptr));
    CHECK_EQ(sceSaveDataDialogOpen(&bar->p), 0);
    for (int i = 0; i < 100; ++i) { SaveUpdate(); Wait(5); }
    CHECK_EQ(SaveUpdate(), kStatusRunning);
    CHECK_EQ(sceSaveDataDialogTerminate(), 0);  // terminate while open
    CHECK_EQ(SaveUpdate(), kStatusNone);
    CHECK(WaitNoDialogs());
    CHECK(!hle::modal::Active());

    CHECK_EQ(sceMsgDialogInitialize(), 0);
    MsgSystem m(kMsgSysPsnChatRestriction);
    CHECK_EQ(sceMsgDialogOpen(&m.p), 0);
    CHECK_EQ(PollUntil(MsgUpdate, kStatusFinished), kStatusFinished);
    MsgDialogResult r{};
    CHECK_EQ(sceMsgDialogGetResult(&r), 0);
    CHECK_EQ(r.buttonId, kMsgButton1);
    CHECK_EQ(sceMsgDialogTerminate(), 0);
    CHECK(WaitNoDialogs());

    if (!fromEnv) ui::SetAutoPressForTest(0);
    CHECK(!hle::modal::Active());
}

// The dialogs centre on the game's SDL window (found by its class, in this process).
void TestCentredOverGameWindow() {
    Section("dialogs centre over the game window");
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    SDL_Window* window = SDL_CreateWindow("dialogtest: stand-in for the game window", 800, 600, 0);
    CHECK(window != nullptr);
    if (!window) return;
    SDL_SetWindowPosition(window, work.left + 40, work.top + 60);  // off-centre on purpose
    SDL_ShowWindow(window);
    for (int i = 0; i < 20; ++i) {
        SDL_PumpEvents();
        Wait(10);
    }
    const HWND game = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    wchar_t cls[32] = {};
    GetClassNameW(game, cls, 32);
    CHECK(std::wcscmp(cls, L"SDL_app") == 0);

    CHECK_EQ(sceMsgDialogInitialize(), 0);
    MsgSystem m(kMsgSysEmptyStore);
    CHECK_EQ(sceMsgDialogOpen(&m.p), 0);
    HWND dlg = FindDialog();
    CHECK(dlg != nullptr);
    RECT g{}, d{};
    GetWindowRect(game, &g);
    GetWindowRect(dlg, &d);
    CHECK(std::abs((g.left + g.right) / 2 - (d.left + d.right) / 2) <= 2);
    CHECK(std::abs((g.top + g.bottom) / 2 - (d.top + d.bottom) / 2) <= 2);
    CHECK(GetWindowLongW(dlg, GWL_EXSTYLE) & WS_EX_TOPMOST);
    Click(dlg, kButton1);
    CHECK_EQ(PollUntil(MsgUpdate, kStatusFinished), kStatusFinished);
    CHECK_EQ(sceMsgDialogTerminate(), 0);
    CHECK(WaitNoDialogs());
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// Runs this exe again with LBP3_AUTOPRESS set, so the dialogs read the real switch.
void RunAutoPressFromEnvironment() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --autopress-env";
    SetEnvironmentVariableW(L"LBP3_AUTOPRESS", L"cross");
    std::fflush(stdout);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    const bool started = CreateProcessW(self, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableW(L"LBP3_AUTOPRESS", nullptr);
    CHECK(started);
    if (!started) return;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CHECK_EQ(code, 0);
}

void TestGameCustomData() {
    Section("GameCustomDataDialog: finishes at once as cancelled (eboot.bin+0xadeda0, +0xaddf40)");
    CHECK_EQ(sceGameCustomDataDialogUpdateStatus(), kStatusNone);
    CHECK_EQ(sceGameCustomDataDialogInitialize(), 0);
    std::vector<uint8_t> param(0x90);
    CHECK_EQ(sceGameCustomDataDialogOpen(param.data()), 0);
    CHECK_EQ(sceGameCustomDataDialogUpdateStatus(), kStatusFinished);
    uint8_t result[0x40] = {};
    CHECK_EQ(sceGameCustomDataDialogGetResult(result), 0);
    int32_t value = 0;
    std::memcpy(&value, result + 0x0C, 4);
    CHECK_EQ(value, kResultUserCanceled);  // the game turns 1 into 0x8002a671 (cancelled), 0 into success
    sceGameCustomDataDialogClose();
    CHECK_EQ(sceGameCustomDataDialogTerminate(), 0);
    CHECK_EQ(sceGameCustomDataDialogUpdateStatus(), kStatusNone);
    CHECK(!sceCommonDialogIsUsed());
}

void TestRegistration() {
    Section("registration covers every import LBP3 has from these libraries");
    struct Import { const char* lib; const char* nid; };
    // From analysis/eboot_imports.json.
    static const Import kImports[] = {
        {"libSceCommonDialog", "uoUpLGNkygk"},
        {"libSceMsgDialog", "6fIC3XKt2k0"}, {"libSceMsgDialog", "b06Hh0DPEaE"},
        {"libSceMsgDialog", "ePw-kqZmelo"}, {"libSceMsgDialog", "lDqxaY1UbEo"},
        {"libSceSaveDataDialog", "4tPhsP6FpDI"}, {"libSceSaveDataDialog", "KK3Bdg1RWK0"},
        {"libSceSaveDataDialog", "YuH2FA7azqQ"}, {"libSceSaveDataDialog", "en7gNVnh878"},
        {"libSceSaveDataDialog", "fH46Lag88XY"}, {"libSceSaveDataDialog", "hay1CfTmLyA"},
        {"libSceSaveDataDialog", "s9e3+YpRnzw"}, {"libSceSaveDataDialog", "yEiJ-qqr6Cg"},
        {"libSceGameCustomDataDialog", "3vyRmRU3CL8"}, {"libSceGameCustomDataDialog", "5TvttyRuU84"},
        {"libSceGameCustomDataDialog", "HwEtHFCpU5U"}, {"libSceGameCustomDataDialog", "PkdLsRA4ON0"},
        {"libSceGameCustomDataDialog", "sJptZwvs1is"}, {"libSceGameCustomDataDialog", "xtbb-2f703A"},
    };
    size_t count = 0;
    const Export* exports = Exports(count);
    for (const auto& imp : kImports) {
        bool found = false;
        for (size_t i = 0; i < count && !found; ++i)
            found = std::strcmp(exports[i].lib, imp.lib) == 0 && core::Nid(exports[i].name) == imp.nid && exports[i].fn;
        if (!found) std::printf("    missing %s %s\n", imp.lib, imp.nid);
        CHECK(found);
    }
    core::Linker linker;
    hle::RegisterDialogs(linker);  // must not throw
}

}  // namespace

// The gate after a dialog closes: the game keeps a neutral pad until the press that
// answered it is released (hle/modal.h), so that press doesn't act on the menu behind.
void TestGateWaitsForRelease() {
    std::printf("gate: held until the answering press is released\n");
    while (GetAsyncKeyState(VK_RETURN) & 0x8000) Sleep(10);  // the release is checked against the real keyboard
    CHECK(!hle::modal::HoldsPad(0));
    hle::modal::Begin("test");
    CHECK(hle::modal::HoldsPad(0));
    hle::modal::End();
    CHECK(!hle::modal::Active());
    CHECK(hle::modal::HoldsPad(ps4::PAD_CROSS));   // Cross still down from answering
    CHECK(hle::modal::HoldsPad(ps4::PAD_CIRCLE));
    CHECK(!hle::modal::HoldsPad(ps4::PAD_SQUARE));  // released: the game gets the pad back
    CHECK(!hle::modal::HoldsPad(ps4::PAD_CROSS));   // and a new Cross is the game's
    hle::modal::Begin("test");
    hle::modal::End();
    Sleep(1100);
    CHECK(!hle::modal::HoldsPad(ps4::PAD_CROSS));   // held past the 1 s cap: the game gets it
}

int main(int argc, char** argv) {
    ui::SetPadReaderForTest(TestPad);
    if (argc == 2 && std::strcmp(argv[1], "--autopress-env") == 0) {  // child of RunAutoPressFromEnvironment
        CHECK(std::getenv("LBP3_AUTOPRESS") != nullptr);
        TestInitialization();
        TestAutoPress(true);
        std::printf("  (autopress child: %d checks, %d failed)\n", g_checks, g_failures);
        return g_failures ? 1 : 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "--capture") == 0) g_captureDir = argv[2];
    ui::SetAutoPressForTest(0);
    TestInitialization();
    TestBusyAndNotInitialized();
    TestMsgDialogQueue();
    TestMsgDialogUser();
    TestSaveFlow();
    TestLoadFlow();
    TestImportListOnDisk();
    TestBootSaveMessages();
    TestAutoPress(false);
    TestCentredOverGameWindow();
    TestGameCustomData();
    TestGateWaitsForRelease();
    TestRegistration();
    RunAutoPressFromEnvironment();
    std::printf("\n%d checks, %d failed: %s\n", g_checks, g_failures, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
