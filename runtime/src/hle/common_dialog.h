// libSceCommonDialog, libSceMsgDialog, libSceSaveDataDialog and
// libSceGameCustomDataDialog. Layouts follow shadPS4's headers, checked against
// how LBP3 fills them (see .scratch/lbp3-port/m2/dialogs.md for the call sites).
#pragma once
#include <cstddef>
#include <cstdint>

#include "hle/abi.h"

namespace hle::common_dialog {

// SceCommonDialogStatus
enum Status : int32_t { kStatusNone = 0, kStatusInitialized = 1, kStatusRunning = 2, kStatusFinished = 3 };
// SceCommonDialogResult
enum Result : int32_t { kResultOk = 0, kResultUserCanceled = 1 };

constexpr int32_t kErrorNotSystemInitialized = static_cast<int32_t>(0x80B80001);
constexpr int32_t kErrorAlreadySystemInitialized = static_cast<int32_t>(0x80B80002);
constexpr int32_t kErrorNotInitialized = static_cast<int32_t>(0x80B80003);
constexpr int32_t kErrorAlreadyInitialized = static_cast<int32_t>(0x80B80004);
constexpr int32_t kErrorNotFinished = static_cast<int32_t>(0x80B80005);
constexpr int32_t kErrorBusy = static_cast<int32_t>(0x80B80008);
constexpr int32_t kErrorParamInvalid = static_cast<int32_t>(0x80B8000A);
constexpr int32_t kErrorNotRunning = static_cast<int32_t>(0x80B8000B);
constexpr int32_t kErrorArgNull = static_cast<int32_t>(0x80B8000D);
constexpr int32_t kErrorNotSupported = static_cast<int32_t>(0x80B8000F);

// Every dialog param starts with this; magic = kBaseParamMagic + low 32 bits of its address.
struct BaseParam {
    uint64_t size;  // sizeof(BaseParam)
    uint8_t reserved[36];
    uint32_t magic;
};
static_assert(sizeof(BaseParam) == 0x30);
constexpr uint32_t kBaseParamMagic = 0xC0D1A109;

// ---- libSceMsgDialog ----
enum MsgMode : int32_t { kMsgModeUser = 1, kMsgModeProgressBar = 2, kMsgModeSystem = 3 };
enum MsgButtonType : int32_t {
    kMsgButtonsOk = 0, kMsgButtonsYesNo = 1, kMsgButtonsNone = 2, kMsgButtonsOkCancel = 3,
    kMsgButtonsWait = 5, kMsgButtonsWaitCancel = 6, kMsgButtonsYesNoFocusNo = 7,
    kMsgButtonsOkCancelFocusCancel = 8, kMsgButtonsTwoButtons = 9,
};
enum MsgSystemType : int32_t {
    kMsgSysEmptyStore = 0, kMsgSysPsnChatRestriction = 1, kMsgSysPsnUgcRestriction = 2,
    kMsgSysCameraNotConnected = 4, kMsgSysProfileNotShared = 5,
};
enum MsgButtonId : int32_t { kMsgButtonInvalid = 0, kMsgButton1 = 1, kMsgButton2 = 2 };  // 1 = OK/Yes, 2 = No

struct MsgButtonsParam {
    const char* msg1;
    const char* msg2;
    char reserved[32];
};
struct MsgUserMessageParam {
    int32_t buttonType;
    int32_t pad;
    const char* msg;  // UTF-8
    MsgButtonsParam* buttonsParam;  // kMsgButtonsTwoButtons
    char reserved[24];
};
struct MsgProgressBarParam {
    int32_t barType;  // 0 = percentage, 1 = percentage with a Cancel button
    int32_t pad;
    const char* msg;
    char reserved[64];
};
struct MsgSystemMessageParam {
    int32_t sysMsgType;
    char reserved[32];
};
struct MsgDialogParam {
    BaseParam baseParam;
    uint64_t size;  // sizeof(MsgDialogParam)
    int32_t mode;
    int32_t pad;
    MsgUserMessageParam* userMsgParam;
    MsgProgressBarParam* progBarParam;
    MsgSystemMessageParam* sysMsgParam;
    int32_t userId;
    char reserved[40];
    int32_t pad2;
};
static_assert(sizeof(MsgDialogParam) == 0x88);
struct MsgDialogResult {
    int32_t mode;
    int32_t result;
    int32_t buttonId;
    char reserved[32];
};

// ---- libSceSaveDataDialog ----
enum SaveMode : int32_t {
    kSaveModeList = 1, kSaveModeUser = 2, kSaveModeSystem = 3, kSaveModeErrorCode = 4, kSaveModeProgressBar = 5,
};
enum SaveDispType : int32_t { kSaveDispSave = 1, kSaveDispLoad = 2, kSaveDispDelete = 3 };
enum SaveButtonType : int32_t { kSaveButtonsOk = 0, kSaveButtonsYesNo = 1, kSaveButtonsNone = 2, kSaveButtonsOkCancel = 3 };
enum SaveButtonId : int32_t { kSaveButtonInvalid = 0, kSaveButtonYes = 1, kSaveButtonNo = 2 };  // OK = Yes
enum SaveSystemType : int32_t {
    kSaveSysNoData = 1, kSaveSysConfirm = 2, kSaveSysOverwrite = 3, kSaveSysNoSpace = 4, kSaveSysProgress = 5,
    kSaveSysFileCorrupted = 6, kSaveSysFinished = 7, kSaveSysNoSpaceContinuable = 8,
    kSaveSysCorruptedRestoreOld = 9,  // not in shadPS4; LBP3 shows it before restoring a backup
    kSaveSysCorruptedAndDeleted = 10, kSaveSysCorruptedAndCreated = 11, kSaveSysCorruptedAndRestore = 13,
    kSaveSysTotalSizeExceeded = 14,
};
enum SaveFocusPos : int32_t {
    kFocusListHead = 0, kFocusListTail = 1, kFocusDataHead = 2, kFocusDataTail = 3,
    kFocusDataLatest = 4, kFocusDataOldest = 5, kFocusDirName = 6,
};

struct SaveDirName {
    char data[32];
};
struct SaveTitleId {
    char data[10];
    char pad[6];
};
struct SaveAnimationParam {
    int32_t userOK;  // 0 = animate, 1 = don't
    int32_t userCancel;
    uint8_t reserved[32];
};
struct SaveNewItem {
    const char* title;  // UTF-8
    void* iconBuf;
    size_t iconSize;
    uint8_t reserved[32];
};
struct SaveItems {
    int32_t userId;
    int32_t pad;
    const SaveTitleId* titleId;
    const SaveDirName* dirName;  // dirNameNum entries
    uint32_t dirNameNum;
    int32_t pad2;
    const SaveNewItem* newItem;  // LIST: the "new saved data" entry
    int32_t focusPos;
    int32_t pad3;
    const SaveDirName* focusPosDirName;
    int32_t itemStyle;
    uint8_t reserved[32];
};
static_assert(sizeof(SaveItems) == 0x60);
struct SaveUserMessageParam {
    int32_t buttonType;
    int32_t msgType;  // 0 = normal, 1 = error
    const char* msg;
    uint8_t reserved[32];
};
struct SaveSystemMessageParam {
    int32_t sysMsgType;
    int32_t pad;
    uint64_t value;  // NOSPACE: blocks (32 KiB) still needed
    uint8_t reserved[32];
};
struct SaveErrorCodeParam {
    int32_t errorCode;
    uint8_t reserved[32];
};
struct SaveProgressBarParam {
    int32_t barType;
    int32_t pad;
    const char* msg;
    int32_t sysMsgType;  // 1 = progress, 2 = restore
    uint8_t reserved[28];
};
struct SaveOptionParam {
    int32_t back;  // 0 = Back/Cancel allowed, 1 = not
    uint8_t reserved[32];
};
struct SaveDataDialogParam {
    BaseParam baseParam;
    int32_t size;  // sizeof(SaveDataDialogParam)
    int32_t mode;
    int32_t dispType;
    int32_t pad;
    SaveAnimationParam* animParam;
    SaveItems* items;
    SaveUserMessageParam* userMsgParam;
    SaveSystemMessageParam* sysMsgParam;
    SaveErrorCodeParam* errorCodeParam;
    SaveProgressBarParam* progressBarParam;
    void* userData;
    SaveOptionParam* optionParam;
    uint8_t reserved[24];
};
static_assert(sizeof(SaveDataDialogParam) == 0x98);
struct SaveDataDialogResult {
    int32_t mode;
    int32_t result;
    int32_t buttonId;
    int32_t pad;
    SaveDirName* dirName;  // optional out: LIST's choice ("" for the new entry or a cancel)
    void* param;           // optional out: SceSaveDataParam of the choice (left untouched: titles aren't kept)
    void* userData;
    uint8_t reserved[32];
};
static_assert(sizeof(SaveDataDialogResult) == 0x48);
struct SaveDataDialogCloseParam {
    int32_t anim;
    uint8_t reserved[32];
};

// ---- libSceCommonDialog ----
int32_t PS4_SYSV_ABI sceCommonDialogInitialize();
bool PS4_SYSV_ABI sceCommonDialogIsUsed();

// ---- libSceMsgDialog ----
int32_t PS4_SYSV_ABI sceMsgDialogInitialize();
int32_t PS4_SYSV_ABI sceMsgDialogOpen(const MsgDialogParam* param);
int32_t PS4_SYSV_ABI sceMsgDialogUpdateStatus();
int32_t PS4_SYSV_ABI sceMsgDialogGetStatus();
int32_t PS4_SYSV_ABI sceMsgDialogGetResult(MsgDialogResult* result);
int32_t PS4_SYSV_ABI sceMsgDialogClose();
int32_t PS4_SYSV_ABI sceMsgDialogTerminate();

// ---- libSceSaveDataDialog ----
int32_t PS4_SYSV_ABI sceSaveDataDialogInitialize();
int32_t PS4_SYSV_ABI sceSaveDataDialogOpen(const SaveDataDialogParam* param);
int32_t PS4_SYSV_ABI sceSaveDataDialogUpdateStatus();
int32_t PS4_SYSV_ABI sceSaveDataDialogGetStatus();
int32_t PS4_SYSV_ABI sceSaveDataDialogGetResult(SaveDataDialogResult* result);
int32_t PS4_SYSV_ABI sceSaveDataDialogClose(const SaveDataDialogCloseParam* param);
int32_t PS4_SYSV_ABI sceSaveDataDialogTerminate();
int32_t PS4_SYSV_ABI sceSaveDataDialogIsReadyToDisplay();
int32_t PS4_SYSV_ABI sceSaveDataDialogProgressBarSetValue(int32_t target, uint32_t rate);
int32_t PS4_SYSV_ABI sceSaveDataDialogProgressBarInc(int32_t target, uint32_t delta);

// ---- libSceGameCustomDataDialog (PSN invitations and game messages: online only) ----
int32_t PS4_SYSV_ABI sceGameCustomDataDialogInitialize();
int32_t PS4_SYSV_ABI sceGameCustomDataDialogOpen(const void* param);
int32_t PS4_SYSV_ABI sceGameCustomDataDialogUpdateStatus();
int32_t PS4_SYSV_ABI sceGameCustomDataDialogGetResult(void* result);
int32_t PS4_SYSV_ABI sceGameCustomDataDialogClose();
int32_t PS4_SYSV_ABI sceGameCustomDataDialogTerminate();
// LBP3 reads the result at +0x0C (sceNpBasicWrapper_Orbis, eboot.bin+0xaddfea): 1 = cancelled.
constexpr size_t kGameCustomDataResultOffset = 0x0C;

// What RegisterDialogs registers, so tests can check the names against the game's imports.
struct Export {
    const char* lib;
    const char* name;
    void* fn;
};
const Export* Exports(size_t& count);

}  // namespace hle::common_dialog
