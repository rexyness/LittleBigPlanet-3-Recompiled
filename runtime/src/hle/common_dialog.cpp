// The PS4's common dialogs: message boxes (libSceMsgDialog) and the save data
// dialogs (libSceSaveDataDialog), drawn here as native windows (hle/dialog_window.h).
//
// As on a PS4: sceCommonDialogInitialize must come first; one common dialog
// library at a time can be initialised (the others get BUSY until it's
// terminated); each library goes NONE -> INITIALIZED -> RUNNING (open) ->
// FINISHED (answered or closed by the game) -> RUNNING again on the next Open,
// and back to NONE on Terminate. The game polls UpdateStatus, often in tight
// loops on worker threads, so that's cheap and never logs.
//
// What LBP3 does with them (call sites in .scratch/lbp3-port/m2/dialogs.md):
// - MsgDialog: system messages only (empty store, chat/UGC restricted), from a queue.
// - SaveDataDialog: profile and level export/import (list, overwrite confirm,
//   progress, no data, corrupted) and the boot save's corrupted/no-space messages.
// - GameCustomDataDialog: PSN invitations; online only, so it finishes as cancelled.
#include "hle/common_dialog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/vfs.h"
#include "hle/dialog_window.h"
#include "hle/kernel_errors.h"
#include "hle/modal.h"

namespace hle::common_dialog {
namespace {

using dialog_ui::Icon;
using dialog_ui::kDismiss;
using dialog_ui::Kind;
using dialog_ui::Outcome;
using dialog_ui::Spec;

constexpr int32_t kOk = kernel::SCE_OK;

std::atomic<bool> g_systemInitialized{false};
enum Owner : int { kOwnerNone = 0, kOwnerMsg = 1, kOwnerSave = 2 };
std::atomic<int> g_owner{kOwnerNone};  // the one common dialog library that's initialised

// One dialog library's state machine. Fields other than `status` are guarded by `m`.
struct Library {
    const char* name;
    int owner;
    std::mutex m;
    std::atomic<int32_t> status{kStatusNone};
    std::shared_ptr<dialog_ui::Window> window;
    std::function<void(const Outcome&)> answer;  // turns the outcome into the library's result
    bool modal = false;                          // holds the modal gate (hle/modal.h)
};

Library g_msg{"msg", kOwnerMsg};
Library g_save{"savedata", kOwnerSave};

int32_t Initialize(Library& lib) {
    if (!g_systemInitialized.load()) return kErrorNotSystemInitialized;
    std::lock_guard lock(lib.m);
    if (lib.status.load() != kStatusNone) return kErrorAlreadyInitialized;
    int expected = kOwnerNone;
    if (!g_owner.compare_exchange_strong(expected, lib.owner) && expected != lib.owner) return kErrorBusy;
    lib.status.store(kStatusInitialized);
    return kOk;
}

// Lock held. Errors for Open in the wrong state.
int32_t CanOpen(const Library& lib) {
    switch (lib.status.load()) {
    case kStatusNone: return kErrorNotInitialized;
    case kStatusRunning: return kErrorBusy;
    default: return kOk;
    }
}

// Lock held, Open validated: shows the window.
void Start(Library& lib, Spec spec, std::function<void(const Outcome&)> answer) {
    lib.window = dialog_ui::Show(std::move(spec));
    lib.answer = std::move(answer);
    if (!lib.modal) {
        modal::Begin(lib.name);
        lib.modal = true;
    }
    lib.status.store(kStatusRunning);
}

// Lock held, status RUNNING.
void Finish(Library& lib, const Outcome& o) {
    if (lib.answer) lib.answer(o);
    lib.answer = nullptr;
    lib.window.reset();
    lib.status.store(kStatusFinished);
    if (lib.modal) {
        modal::End();
        lib.modal = false;
    }
}

int32_t UpdateStatus(Library& lib) {
    if (lib.status.load() != kStatusRunning) return lib.status.load();
    std::lock_guard lock(lib.m);
    Outcome o;
    if (lib.status.load() == kStatusRunning && lib.window && dialog_ui::Poll(*lib.window, o)) Finish(lib, o);
    return lib.status.load();
}

// The game closes a running dialog (progress bars when the work is done).
int32_t Close(Library& lib) {
    std::lock_guard lock(lib.m);
    if (lib.status.load() != kStatusRunning) return kErrorNotRunning;
    if (lib.window) dialog_ui::Close(*lib.window);
    std::fprintf(stderr, "[dialog] %s: closed by the game\n", lib.name);
    Finish(lib, Outcome{});
    return kOk;
}

int32_t Terminate(Library& lib) {
    std::lock_guard lock(lib.m);
    if (lib.status.load() == kStatusNone) return kErrorNotInitialized;
    if (lib.status.load() == kStatusRunning) {
        if (lib.window) dialog_ui::Close(*lib.window);
        std::fprintf(stderr, "[dialog] %s: terminated while open\n", lib.name);
        Finish(lib, Outcome{});
    }
    lib.status.store(kStatusNone);
    int expected = lib.owner;
    g_owner.compare_exchange_strong(expected, kOwnerNone);
    return kOk;
}

bool ValidBase(const BaseParam& b) {
    return b.size == sizeof(BaseParam) &&
           b.magic == kBaseParamMagic + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&b));
}

std::wstring Wide(const char* s, size_t max = 1024) {
    if (!s) return {};
    const int len = static_cast<int>(strnlen(s, max));
    if (len == 0) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, len, w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// First line of a message, for the log.
std::string Brief(const std::wstring& text) {
    std::string s = Narrow(text.substr(0, text.find(L'\n')));
    return s.size() > 90 ? s.substr(0, 87) + "..." : s;
}

// ---------------------------------------------------------------- MsgDialog

MsgDialogResult g_msgResult{};  // guarded by g_msg.m

const wchar_t* MsgSystemText(int32_t type) {
    switch (type) {
    case kMsgSysEmptyStore: return L"There is no content available in PlayStation Store yet.";
    case kMsgSysPsnChatRestriction: return L"Chat is restricted for this account by its parental control settings.";
    case kMsgSysPsnUgcRestriction:
        return L"Content created by other players is restricted for this account by its parental control settings.";
    case kMsgSysCameraNotConnected: return L"Connect the PlayStation Camera.";
    case kMsgSysProfileNotShared: return L"Your profile picture and name are not shared with other players.";
    default: return nullptr;
    }
}

// USER_MSG buttons. Cross and Enter press `defaultButton`; Circle and Esc cancel
// where there is a cancel; LBP3_AUTOPRESS presses the answer that changes nothing.
bool MsgUserButtons(int32_t type, const MsgButtonsParam* labels, Spec& s) {
    switch (type) {
    case kMsgButtonsOk: s.button1 = L"OK"; s.cancelButton = 1; s.autoAnswer = 1; return true;
    case kMsgButtonsYesNo:
    case kMsgButtonsYesNoFocusNo:
        s.button1 = L"Yes"; s.button2 = L"No"; s.cancelButton = 2; s.autoAnswer = 2;
        s.defaultButton = type == kMsgButtonsYesNoFocusNo ? 2 : 1;
        s.icon = Icon::Question;
        return true;
    case kMsgButtonsOkCancel:
    case kMsgButtonsOkCancelFocusCancel:
        s.button1 = L"OK"; s.button2 = L"Cancel"; s.cancelButton = 2; s.autoAnswer = 2;
        s.defaultButton = type == kMsgButtonsOkCancelFocusCancel ? 2 : 1;
        return true;
    case kMsgButtonsNone:
    case kMsgButtonsWait: return true;  // the game closes it
    case kMsgButtonsWaitCancel: s.button1 = L"Cancel"; s.cancelButton = 1; return true;
    case kMsgButtonsTwoButtons:
        if (!labels) return false;
        s.button1 = Wide(labels->msg1, 64); s.button2 = Wide(labels->msg2, 64);
        s.cancelButton = kDismiss; s.autoAnswer = kDismiss;
        return true;
    default: return false;
    }
}

MsgDialogResult MsgResultFor(int32_t mode, int32_t buttonType, const Outcome& o) {
    MsgDialogResult r{};
    r.mode = mode;
    const auto cancelled = [&] { r.result = kResultUserCanceled; r.buttonId = kMsgButtonInvalid; };
    if (o.button == 0) return r;  // closed by the game
    if (o.button == kDismiss) {
        cancelled();
        return r;
    }
    if (mode == kMsgModeSystem) {
        r.buttonId = kMsgButton1;
    } else if (mode == kMsgModeProgressBar || buttonType == kMsgButtonsWaitCancel) {
        cancelled();  // their only button is Cancel
    } else if (o.button == 1) {
        r.buttonId = kMsgButton1;
    } else if (buttonType == kMsgButtonsOkCancel || buttonType == kMsgButtonsOkCancelFocusCancel) {
        cancelled();
    } else {
        r.buttonId = kMsgButton2;
    }
    return r;
}

// ---------------------------------------------------------------- SaveDataDialog

struct SaveResult {
    int32_t mode = 0, result = kResultOk, buttonId = kSaveButtonInvalid;
    std::string dirName;
    void* userData = nullptr;
};
SaveResult g_saveResult;  // guarded by g_save.m

std::string DirNameString(const SaveDirName& d) { return std::string(d.data, strnlen(d.data, sizeof(d.data))); }

// The save folder, as hle/savedata.cpp lays them out.
std::filesystem::path SaveFolder(const SaveItems* items, const std::string& dirName) {
    std::string title = items && items->titleId ? std::string(items->titleId->data, strnlen(items->titleId->data, 10)) : "";
    if (title.empty()) title = "CUSA00473";
    return core::vfs::UserRoot() / "savedata" / title / std::filesystem::path(std::u8string(dirName.begin(), dirName.end()));
}

struct SaveEntry {
    std::string dirName;
    std::wstring label;
    int64_t modified = 0;  // seconds since 1970, 0 = unknown
};

SaveEntry DescribeSave(const SaveItems* items, const std::string& dirName) {
    SaveEntry e{dirName, Wide(dirName.c_str(), 32), 0};
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(SaveFolder(items, dirName), ec);
    if (ec) return e;
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    e.modified = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    const std::time_t tt = static_cast<std::time_t>(e.modified);
    std::tm tm{};
    if (localtime_s(&tm, &tt) == 0) {
        wchar_t when[64];
        std::wcsftime(when, 64, L"%Y-%m-%d %H:%M", &tm);
        e.label += L"    ";
        e.label += when;
    }
    return e;
}

std::wstring SizeText(uint64_t blocks) {
    const double mb = static_cast<double>(blocks) * 32768.0 / (1024.0 * 1024.0);
    wchar_t s[64];
    std::swprintf(s, 64, mb >= 1.0 ? L"%.0f MB" : L"%.2f MB", mb);
    return s;
}

const wchar_t* ByDispType(int32_t disp, const wchar_t* save, const wchar_t* load, const wchar_t* del) {
    return disp == kSaveDispLoad ? load : disp == kSaveDispDelete ? del : save;
}

// What one system message looks like and how its buttons map to a result. The
// game was built with SDK 2.00; shadPS4 notes that before 4.50 "No" and the OK
// of the "nothing happened" messages come back as USER_CANCELED.
struct SystemMessage {
    std::wstring text;
    Icon icon = Icon::Info;
    enum Buttons { kOkOnly, kYesNo, kOkCancel, kNoButtons, kCancelOnly } buttons = kOkOnly;
    int defaultButton = 1;
    bool okIsCancel = false;
};

SystemMessage DescribeSystemMessage(const SaveDataDialogParam& p) {
    const SaveSystemMessageParam& sys = *p.sysMsgParam;
    const int32_t disp = p.dispType;
    const bool backDisabled = p.optionParam && p.optionParam->back == 1;
    SystemMessage m;
    switch (sys.sysMsgType) {
    case kSaveSysNoData:
        m.text = L"There is no saved data.";
        m.okIsCancel = true;
        break;
    case kSaveSysConfirm:
        m.text = ByDispType(disp, L"Do you want to save?", L"Do you want to load this saved data?",
                            L"Do you want to delete this saved data?");
        m.icon = Icon::Question;
        m.buttons = SystemMessage::kYesNo;
        m.defaultButton = disp == kSaveDispDelete ? 2 : 1;
        break;
    case kSaveSysOverwrite:
        m.text = L"Do you want to overwrite this saved data?";
        m.icon = Icon::Warning;
        m.buttons = SystemMessage::kYesNo;
        m.defaultButton = 2;  // the destructive answer is never the default
        break;
    case kSaveSysNoSpace:
        m.text = L"There is not enough free space to save. At least " + SizeText(sys.value) + L" more is needed.";
        m.icon = Icon::Warning;
        m.okIsCancel = true;
        break;
    case kSaveSysProgress:
        m.text = ByDispType(disp, L"Saving...", L"Loading...", L"Deleting...");
        m.icon = Icon::None;
        m.buttons = p.optionParam && p.optionParam->back == 0 ? SystemMessage::kCancelOnly : SystemMessage::kNoButtons;
        break;
    case kSaveSysFileCorrupted:
        m.text = L"The saved data is corrupted and can't be used.";
        m.icon = Icon::Error;
        m.okIsCancel = true;
        break;
    case kSaveSysFinished:
        m.text = ByDispType(disp, L"Saving complete.", L"Loading complete.", L"Deletion complete.");
        m.okIsCancel = true;
        break;
    case kSaveSysNoSpaceContinuable:
        m.text = L"There is not enough free space to save. " + SizeText(sys.value) + L" more is needed.";
        m.icon = Icon::Warning;
        m.okIsCancel = true;
        break;
    case kSaveSysCorruptedRestoreOld:
        m.text = L"The saved data is corrupted. The game will now try to restore the backup copy the system made.";
        m.icon = Icon::Warning;
        break;
    case kSaveSysCorruptedAndDeleted:
        m.text = L"The saved data is corrupted and will be deleted.";
        m.icon = Icon::Warning;
        m.buttons = backDisabled ? SystemMessage::kOkOnly : SystemMessage::kOkCancel;
        m.defaultButton = backDisabled ? 1 : 2;
        break;
    case kSaveSysCorruptedAndCreated:
        m.text = L"The saved data is corrupted. It will be deleted and new saved data will be created.";
        m.icon = Icon::Warning;
        m.buttons = backDisabled ? SystemMessage::kOkOnly : SystemMessage::kOkCancel;
        m.defaultButton = backDisabled ? 1 : 2;
        break;
    case kSaveSysCorruptedAndRestore:
        m.text = L"The saved data is corrupted. The backup copy the system made will be restored.";
        m.icon = Icon::Warning;
        m.buttons = backDisabled ? SystemMessage::kOkOnly : SystemMessage::kOkCancel;
        break;
    case kSaveSysTotalSizeExceeded:
        m.text = L"No more saved data can be created.";
        m.icon = Icon::Warning;
        break;
    default:
        m.text = L"Saved data message " + std::to_wstring(sys.sysMsgType) + L".";
        break;
    }
    return m;
}

std::wstring WithDirName(std::wstring text, const SaveItems* items) {
    if (items && items->dirName && items->dirNameNum > 0) {
        const std::string name = DirNameString(items->dirName[0]);
        if (!name.empty()) text += L"\n\nSaved data: " + Wide(name.c_str(), 32);
    }
    return text;
}

// LIST mode: the saved data the game found, plus its "new saved data" entry.
int32_t OpenList(const SaveDataDialogParam& p, Spec& s, std::function<void(const Outcome&)>& answer) {
    const SaveItems* items = p.items;
    if (!items) return kErrorParamInvalid;
    std::vector<SaveEntry> entries;
    const bool hasNew = items->newItem != nullptr;
    if (hasNew) {
        std::wstring label = L"[New saved data]";
        const std::wstring title = Wide(items->newItem->title, 128);
        if (!title.empty()) label += L"  " + title;
        entries.push_back({"", label, 0});
    }
    for (uint32_t i = 0; items->dirName && i < items->dirNameNum; ++i) {
        const std::string name = DirNameString(items->dirName[i]);
        if (!name.empty()) entries.push_back(DescribeSave(items, name));
    }
    if (entries.empty()) entries.push_back({"", L"(no saved data)", 0});

    const int first = hasNew ? 1 : 0, last = static_cast<int>(entries.size()) - 1;
    int focus = 0;
    switch (items->focusPos) {
    case kFocusListTail: focus = last; break;
    case kFocusDataHead: focus = std::min(first, last); break;
    case kFocusDataTail: focus = last; break;
    case kFocusDataLatest:
    case kFocusDataOldest: {
        focus = std::min(first, last);
        for (int i = first; i <= last; ++i) {
            const bool better = items->focusPos == kFocusDataLatest ? entries[i].modified > entries[focus].modified
                                                                    : entries[i].modified < entries[focus].modified;
            if (better && entries[i].modified) focus = i;
        }
        break;
    }
    case kFocusDirName:
        if (items->focusPosDirName)
            for (int i = first; i <= last; ++i)
                if (entries[i].dirName == DirNameString(*items->focusPosDirName)) focus = i;
        break;
    default: break;  // list head
    }

    s.kind = Kind::List;
    s.icon = Icon::None;
    s.text = ByDispType(p.dispType, L"Choose where to save.", L"Choose the saved data to load.",
                        L"Choose the saved data to delete.");
    s.button1 = ByDispType(p.dispType, L"Save", L"Load", L"Delete");
    s.button2 = L"Cancel";
    s.cancelButton = 2;
    s.autoAnswer = 2;  // loading replaces what's in memory; saving could overwrite: cancel changes nothing
    for (const auto& e : entries) s.items.push_back(e.label);
    s.focus = focus;
    std::fprintf(stderr, "[dialog] savedata: list (%s), %u saved data%s\n",
                 p.dispType == kSaveDispLoad ? "load" : p.dispType == kSaveDispDelete ? "delete" : "save",
                 static_cast<unsigned>(entries.size() - (hasNew ? 1 : 0)), hasNew ? " + new" : "");

    std::vector<std::string> names;
    for (const auto& e : entries) names.push_back(e.dirName);
    answer = [names](const Outcome& o) {
        if (o.button == 1 && o.item >= 0 && o.item < static_cast<int>(names.size())) {
            g_saveResult.dirName = names[o.item];  // "" = the new entry: the game names it
        } else if (o.button != 0) {
            g_saveResult.result = kResultUserCanceled;
        }
    };
    return kOk;
}

int32_t OpenSystemMessage(const SaveDataDialogParam& p, Spec& s, std::function<void(const Outcome&)>& answer) {
    if (!p.sysMsgParam) return kErrorParamInvalid;
    const SystemMessage m = DescribeSystemMessage(p);
    s.text = WithDirName(m.text, p.items);
    s.icon = m.icon;
    s.defaultButton = m.defaultButton;
    switch (m.buttons) {
    case SystemMessage::kOkOnly: s.button1 = L"OK"; s.cancelButton = 1; s.autoAnswer = 1; break;
    case SystemMessage::kYesNo: s.button1 = L"Yes"; s.button2 = L"No"; s.cancelButton = 2; s.autoAnswer = 2; break;
    case SystemMessage::kOkCancel: s.button1 = L"OK"; s.button2 = L"Cancel"; s.cancelButton = 2; s.autoAnswer = 2; break;
    case SystemMessage::kCancelOnly: s.kind = Kind::Progress; s.button1 = L"Cancel"; s.cancelButton = 1; break;
    case SystemMessage::kNoButtons: s.kind = Kind::Progress; break;
    }
    std::fprintf(stderr, "[dialog] savedata: system message %d: %s\n", p.sysMsgParam->sysMsgType, Brief(m.text).c_str());
    const auto buttons = m.buttons;
    const bool okIsCancel = m.okIsCancel;
    answer = [buttons, okIsCancel](const Outcome& o) {
        if (o.button == 0) return;  // closed by the game
        const bool cancelled = o.button == kDismiss || o.button == 2 || buttons == SystemMessage::kCancelOnly ||
                               (buttons == SystemMessage::kOkOnly && okIsCancel);
        if (cancelled) {
            g_saveResult.result = kResultUserCanceled;
        } else {
            g_saveResult.buttonId = kSaveButtonYes;
        }
    };
    return kOk;
}

int32_t OpenUserMessage(const SaveDataDialogParam& p, Spec& s, std::function<void(const Outcome&)>& answer) {
    const SaveUserMessageParam* u = p.userMsgParam;
    if (!u) return kErrorParamInvalid;
    s.text = WithDirName(Wide(u->msg), p.items);
    s.icon = u->msgType == 1 ? Icon::Error : Icon::Info;
    const int32_t type = u->buttonType;
    switch (type) {
    case kSaveButtonsOk: s.button1 = L"OK"; s.cancelButton = 1; s.autoAnswer = 1; break;
    case kSaveButtonsYesNo:
        s.button1 = L"Yes"; s.button2 = L"No"; s.cancelButton = 2; s.autoAnswer = 2; s.icon = Icon::Question;
        break;
    case kSaveButtonsOkCancel: s.button1 = L"OK"; s.button2 = L"Cancel"; s.cancelButton = 2; s.autoAnswer = 2; break;
    case kSaveButtonsNone: s.kind = Kind::Progress; break;
    default: return kErrorParamInvalid;
    }
    std::fprintf(stderr, "[dialog] savedata: user message: %s\n", Brief(s.text).c_str());
    answer = [type](const Outcome& o) {
        if (o.button == 0) return;
        if (o.button == 1) {
            g_saveResult.buttonId = kSaveButtonYes;
        } else if (type == kSaveButtonsYesNo && o.button == 2) {
            g_saveResult.buttonId = kSaveButtonNo;
        } else {
            g_saveResult.result = kResultUserCanceled;
        }
    };
    return kOk;
}

int32_t OpenErrorCode(const SaveDataDialogParam& p, Spec& s, std::function<void(const Outcome&)>& answer) {
    if (!p.errorCodeParam) return kErrorParamInvalid;
    wchar_t text[160];
    std::swprintf(text, 160, L"Something went wrong with the saved data.\n\nError code: %08X",
                  static_cast<uint32_t>(p.errorCodeParam->errorCode));
    s.text = WithDirName(text, p.items);
    s.icon = Icon::Error;
    s.button1 = L"OK";
    s.cancelButton = 1;
    s.autoAnswer = 1;
    std::fprintf(stderr, "[dialog] savedata: error code %08X\n", static_cast<uint32_t>(p.errorCodeParam->errorCode));
    answer = [](const Outcome& o) {
        if (o.button != 0) g_saveResult.buttonId = kSaveButtonYes;
    };
    return kOk;
}

int32_t OpenProgressBar(const SaveDataDialogParam& p, Spec& s, std::function<void(const Outcome&)>& answer) {
    const SaveProgressBarParam* bar = p.progressBarParam;
    if (!bar) return kErrorParamInvalid;
    std::wstring text = Wide(bar->msg);
    if (text.empty())
        text = bar->sysMsgType == 2 ? L"Restoring saved data..."
                                    : ByDispType(p.dispType, L"Saving...", L"Loading...", L"Deleting...");
    s.kind = Kind::Progress;
    s.icon = Icon::None;
    s.text = WithDirName(text, p.items);
    std::fprintf(stderr, "[dialog] savedata: progress bar: %s\n", Brief(text).c_str());
    answer = [](const Outcome&) {};
    return kOk;
}

// ---------------------------------------------------------------- GameCustomDataDialog

std::atomic<int32_t> g_customStatus{kStatusNone};

}  // namespace

// ================================================================ libSceCommonDialog

int32_t PS4_SYSV_ABI sceCommonDialogInitialize() {
    // LBP3 calls this twice: from the save system at boot, which also accepts 0x80B80004,
    // and from its NP setup, which expects ALREADY_SYSTEM_INITIALIZED (eboot.bin+0x33793d).
    if (g_systemInitialized.exchange(true)) return kErrorAlreadySystemInitialized;
    return kOk;
}

bool PS4_SYSV_ABI sceCommonDialogIsUsed() { return g_owner.load() != kOwnerNone; }

// ================================================================ libSceMsgDialog

int32_t PS4_SYSV_ABI sceMsgDialogInitialize() { return Initialize(g_msg); }

int32_t PS4_SYSV_ABI sceMsgDialogOpen(const MsgDialogParam* param) {
    std::lock_guard lock(g_msg.m);
    if (const int32_t e = CanOpen(g_msg)) return e;
    if (!param) return kErrorArgNull;
    if (param->size != sizeof(MsgDialogParam) || !ValidBase(param->baseParam)) return kErrorParamInvalid;

    Spec s;
    s.logName = "msg";
    const int32_t mode = param->mode;
    int32_t buttonType = -1;
    switch (mode) {
    case kMsgModeUser: {
        const MsgUserMessageParam* u = param->userMsgParam;
        if (!u) return kErrorParamInvalid;
        buttonType = u->buttonType;
        s.text = Wide(u->msg);
        if (!MsgUserButtons(buttonType, u->buttonsParam, s)) return kErrorParamInvalid;
        if (buttonType == kMsgButtonsNone || buttonType == kMsgButtonsWait || buttonType == kMsgButtonsWaitCancel)
            s.kind = Kind::Progress;  // no answer expected: don't take the keyboard focus
        std::fprintf(stderr, "[dialog] msg: user message (buttons %d): %s\n", buttonType, Brief(s.text).c_str());
        break;
    }
    case kMsgModeProgressBar: {
        const MsgProgressBarParam* bar = param->progBarParam;
        if (!bar) return kErrorParamInvalid;
        s.kind = Kind::Progress;
        s.icon = Icon::None;
        s.text = Wide(bar->msg);
        if (bar->barType == 1) {
            s.button1 = L"Cancel";
            s.cancelButton = 1;
        }
        std::fprintf(stderr, "[dialog] msg: progress bar: %s\n", Brief(s.text).c_str());
        break;
    }
    case kMsgModeSystem: {
        const MsgSystemMessageParam* sys = param->sysMsgParam;
        if (!sys) return kErrorParamInvalid;
        const wchar_t* text = MsgSystemText(sys->sysMsgType);
        s.text = text ? text : L"System message " + std::to_wstring(sys->sysMsgType) + L".";
        s.button1 = L"OK";
        s.cancelButton = 1;
        s.autoAnswer = 1;
        std::fprintf(stderr, "[dialog] msg: system message %d: %s\n", sys->sysMsgType, Brief(s.text).c_str());
        break;
    }
    default: return kErrorParamInvalid;
    }
    g_msgResult = {};
    g_msgResult.mode = mode;
    Start(g_msg, std::move(s), [mode, buttonType](const Outcome& o) {
        g_msgResult = MsgResultFor(mode, buttonType, o);
        if (o.button != 0)
            std::fprintf(stderr, "[dialog] msg: answered (result %d, button %d)\n", g_msgResult.result, g_msgResult.buttonId);
    });
    return kOk;
}

int32_t PS4_SYSV_ABI sceMsgDialogUpdateStatus() { return UpdateStatus(g_msg); }
int32_t PS4_SYSV_ABI sceMsgDialogGetStatus() { return g_msg.status.load(); }

int32_t PS4_SYSV_ABI sceMsgDialogGetResult(MsgDialogResult* result) {
    std::lock_guard lock(g_msg.m);
    if (g_msg.status.load() != kStatusFinished) return kErrorNotFinished;
    if (!result) return kErrorArgNull;
    *result = g_msgResult;
    return kOk;
}

int32_t PS4_SYSV_ABI sceMsgDialogClose() { return Close(g_msg); }
int32_t PS4_SYSV_ABI sceMsgDialogTerminate() { return Terminate(g_msg); }

// ================================================================ libSceSaveDataDialog

int32_t PS4_SYSV_ABI sceSaveDataDialogInitialize() { return Initialize(g_save); }

int32_t PS4_SYSV_ABI sceSaveDataDialogOpen(const SaveDataDialogParam* param) {
    std::lock_guard lock(g_save.m);
    if (const int32_t e = CanOpen(g_save)) return e;
    if (!param) return kErrorArgNull;
    if (param->size != static_cast<int32_t>(sizeof(SaveDataDialogParam)) || !ValidBase(param->baseParam))
        return kErrorParamInvalid;

    Spec s;
    s.logName = "savedata";
    std::function<void(const Outcome&)> answer;
    int32_t e = kErrorParamInvalid;
    switch (param->mode) {
    case kSaveModeList: e = OpenList(*param, s, answer); break;
    case kSaveModeUser: e = OpenUserMessage(*param, s, answer); break;
    case kSaveModeSystem: e = OpenSystemMessage(*param, s, answer); break;
    case kSaveModeErrorCode: e = OpenErrorCode(*param, s, answer); break;
    case kSaveModeProgressBar: e = OpenProgressBar(*param, s, answer); break;
    default: break;
    }
    if (e != kOk) return e;
    g_saveResult = {};
    g_saveResult.mode = param->mode;
    g_saveResult.userData = param->userData;
    Start(g_save, std::move(s), [answer](const Outcome& o) {
        answer(o);
        if (o.button != 0)
            std::fprintf(stderr, "[dialog] savedata: answered (result %d, button %d%s%s)\n", g_saveResult.result,
                         g_saveResult.buttonId, g_saveResult.dirName.empty() ? "" : ", ",
                         g_saveResult.dirName.c_str());
    });
    return kOk;
}

int32_t PS4_SYSV_ABI sceSaveDataDialogUpdateStatus() { return UpdateStatus(g_save); }
int32_t PS4_SYSV_ABI sceSaveDataDialogGetStatus() { return g_save.status.load(); }

int32_t PS4_SYSV_ABI sceSaveDataDialogGetResult(SaveDataDialogResult* result) {
    std::lock_guard lock(g_save.m);
    if (g_save.status.load() != kStatusFinished) return kErrorNotFinished;
    if (!result) return kErrorArgNull;
    result->mode = g_saveResult.mode;
    result->result = g_saveResult.result;
    result->buttonId = g_saveResult.buttonId;
    if (result->dirName) {
        std::memset(result->dirName->data, 0, sizeof(result->dirName->data));
        std::memcpy(result->dirName->data, g_saveResult.dirName.data(),
                    std::min(g_saveResult.dirName.size(), sizeof(result->dirName->data) - 1));
    }
    result->userData = g_saveResult.userData;
    return kOk;
}

int32_t PS4_SYSV_ABI sceSaveDataDialogClose(const SaveDataDialogCloseParam*) { return Close(g_save); }
int32_t PS4_SYSV_ABI sceSaveDataDialogTerminate() { return Terminate(g_save); }
int32_t PS4_SYSV_ABI sceSaveDataDialogIsReadyToDisplay() { return 1; }

namespace {
uint32_t g_progress = 0;  // guarded by g_save.m

int32_t SetProgress(int32_t target, uint32_t value) {
    std::lock_guard lock(g_save.m);
    if (g_save.status.load() != kStatusRunning) return kErrorNotRunning;
    if (g_saveResult.mode != kSaveModeProgressBar) return kErrorNotSupported;
    if (target != 0) return kErrorParamInvalid;
    g_progress = std::min<uint32_t>(value, 100);
    if (g_save.window) dialog_ui::SetProgress(*g_save.window, g_progress);
    return kOk;
}
}  // namespace

int32_t PS4_SYSV_ABI sceSaveDataDialogProgressBarSetValue(int32_t target, uint32_t rate) { return SetProgress(target, rate); }

int32_t PS4_SYSV_ABI sceSaveDataDialogProgressBarInc(int32_t target, uint32_t delta) {
    uint32_t now;
    {
        std::lock_guard lock(g_save.m);
        now = g_progress;
    }
    return SetProgress(target, now + delta);
}

// ================================================================ libSceGameCustomDataDialog
// LBP3 opens it from sceNpBasicWrapper_Orbis to send or view PSN invitations, then
// polls it every frame and calls back with the result; a dialog that never finished
// would leave that send pending for good. Nothing can be sent offline, so it finishes
// at once as cancelled.

int32_t PS4_SYSV_ABI sceGameCustomDataDialogInitialize() {
    int32_t expected = kStatusNone;
    if (!g_customStatus.compare_exchange_strong(expected, kStatusInitialized)) return kErrorAlreadyInitialized;
    return kOk;
}

int32_t PS4_SYSV_ABI sceGameCustomDataDialogOpen(const void* param) {
    if (!param) return kErrorArgNull;
    const int32_t status = g_customStatus.load();
    if (status == kStatusNone) return kErrorNotInitialized;
    if (status == kStatusRunning) return kErrorBusy;
    std::fprintf(stderr, "[dialog] game custom data (PSN invitations) needs PlayStation Network: closed as cancelled\n");
    g_customStatus.store(kStatusFinished);
    return kOk;
}

int32_t PS4_SYSV_ABI sceGameCustomDataDialogUpdateStatus() { return g_customStatus.load(); }

int32_t PS4_SYSV_ABI sceGameCustomDataDialogGetResult(void* result) {
    if (g_customStatus.load() != kStatusFinished) return kErrorNotFinished;
    if (!result) return kErrorArgNull;
    const int32_t cancelled = kResultUserCanceled;
    std::memcpy(static_cast<uint8_t*>(result) + kGameCustomDataResultOffset, &cancelled, sizeof(cancelled));
    return kOk;
}

int32_t PS4_SYSV_ABI sceGameCustomDataDialogClose() {
    return g_customStatus.load() == kStatusRunning ? (g_customStatus.store(kStatusFinished), kOk) : kErrorNotRunning;
}

int32_t PS4_SYSV_ABI sceGameCustomDataDialogTerminate() {
    if (g_customStatus.exchange(kStatusNone) == kStatusNone) return kErrorNotInitialized;
    return kOk;
}

// ================================================================ registration

#define EXPORT(lib, fn) Export{lib, #fn, reinterpret_cast<void*>(&fn)}

const Export* Exports(size_t& count) {
    static const Export kExports[] = {
        EXPORT("libSceCommonDialog", sceCommonDialogInitialize),
        EXPORT("libSceCommonDialog", sceCommonDialogIsUsed),
        EXPORT("libSceMsgDialog", sceMsgDialogInitialize),
        EXPORT("libSceMsgDialog", sceMsgDialogOpen),
        EXPORT("libSceMsgDialog", sceMsgDialogUpdateStatus),
        EXPORT("libSceMsgDialog", sceMsgDialogGetStatus),
        EXPORT("libSceMsgDialog", sceMsgDialogGetResult),
        EXPORT("libSceMsgDialog", sceMsgDialogClose),
        EXPORT("libSceMsgDialog", sceMsgDialogTerminate),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogInitialize),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogOpen),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogUpdateStatus),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogGetStatus),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogGetResult),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogClose),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogTerminate),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogIsReadyToDisplay),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogProgressBarSetValue),
        EXPORT("libSceSaveDataDialog", sceSaveDataDialogProgressBarInc),
        // The import library is libSceGameCustomDataDialog (module libGameCustomDataDialog).
        EXPORT("libSceGameCustomDataDialog", sceGameCustomDataDialogInitialize),
        EXPORT("libSceGameCustomDataDialog", sceGameCustomDataDialogOpen),
        EXPORT("libSceGameCustomDataDialog", sceGameCustomDataDialogUpdateStatus),
        EXPORT("libSceGameCustomDataDialog", sceGameCustomDataDialogGetResult),
        EXPORT("libSceGameCustomDataDialog", sceGameCustomDataDialogClose),
        EXPORT("libSceGameCustomDataDialog", sceGameCustomDataDialogTerminate),
    };
    count = sizeof(kExports) / sizeof(kExports[0]);
    return kExports;
}

#undef EXPORT

}  // namespace hle::common_dialog
