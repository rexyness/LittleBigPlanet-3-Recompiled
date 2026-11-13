// libSceUserService and libSceSystemService: local players and console settings that match
// a stock English PS4. User 1 is always signed in; users 2-4 sign in while a controller is in
// their pad slot (local co-op; input/gamepad.h: user N plays with slot N-1) and sign out when
// it goes. Each sign-in and sign-out is an event for sceUserServiceGetEvent, in order.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hle/kernel_errors.h"
#include "hle/language.h"
#include "hle/register.h"
#include "hle/users.h"
#include "input/gamepad.h"

namespace hle::user {

constexpr int kMaxLoginUsers = 4;
struct LoginUserIdList { int32_t userId[kMaxLoginUsers]; };
struct UserEvent { int32_t eventType; int32_t userId; };

namespace {

constexpr int32_t kUserId = 1;  // player 1; user N plays with pad slot N-1
constexpr int32_t kInvalidUser = -1;
constexpr int32_t kEventLogin = 0, kEventLogout = 1;
constexpr int kErrorNoEvent = static_cast<int>(0x80960007);
constexpr int kErrorInvalidArgument = static_cast<int>(0x80960002);

std::mutex g_mutex;
std::deque<UserEvent> g_events;
bool g_started = false;                  // user 1's login has been queued
bool g_in[kMaxLoginUsers + 1] = {};      // by user id: signed in
uint32_t g_gen[kMaxLoginUsers + 1] = {}; // by user id: the pad slot's generation at sign-in

// Under g_mutex: turns controller changes since the last look into sign-in and sign-out
// events. A slot whose controller dropped out and came back in between (a new generation)
// signs its user out and in again.
void SyncLocked() {
    if (!g_started) {
        g_started = true;
        g_in[kUserId] = true;
        g_events.push_back({kEventLogin, kUserId});
    }
    input::Refresh();
    for (int slot = 1; slot < input::kMaxPads && slot + 1 <= kMaxLoginUsers; ++slot) {
        const int32_t user = slot + 1;
        uint32_t gen = 0;
        const bool present = input::SlotPresent(slot, &gen);
        if (present == g_in[user] && (!present || gen == g_gen[user])) continue;
        if (g_in[user]) {
            g_in[user] = false;
            g_events.push_back({kEventLogout, user});
            np::DropStateNotice(user);
            std::fprintf(stderr, "[user] user %d logged out (slot %d)\n", user, slot);
        }
        if (present) {
            g_in[user] = true;
            g_gen[user] = gen;
            g_events.push_back({kEventLogin, user});
            std::fprintf(stderr, "[user] user %d logged in (slot %d, %s)\n", user, slot, input::SlotName(slot).c_str());
            np::QueueStateNotice(user);
        }
    }
}

}  // namespace

int PS4_SYSV_ABI sceUserServiceInitialize(const void*) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceUserServiceTerminate() { return kernel::SCE_OK; }

int PS4_SYSV_ABI sceUserServiceGetInitialUser(int32_t* userId) {
    if (!userId) return kErrorInvalidArgument;
    *userId = kUserId;
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceUserServiceGetLoginUserIdList(LoginUserIdList* list) {
    if (!list) return kErrorInvalidArgument;
    std::lock_guard lock(g_mutex);
    SyncLocked();
    int n = 0;
    for (int32_t user = 1; user <= kMaxLoginUsers; ++user)
        if (g_in[user]) list->userId[n++] = user;
    while (n < kMaxLoginUsers) list->userId[n++] = kInvalidUser;
    return kernel::SCE_OK;
}

// User 1's login first, then every sign-in and sign-out as controllers come and go.
int PS4_SYSV_ABI sceUserServiceGetEvent(UserEvent* ev) {
    if (!ev) return kErrorInvalidArgument;
    std::lock_guard lock(g_mutex);
    SyncLocked();
    if (g_events.empty()) return kErrorNoEvent;
    *ev = g_events.front();
    g_events.pop_front();
    return kernel::SCE_OK;
}

// A PS4 user name is at most 16 bytes (SCE_USER_SERVICE_MAX_USER_NAME_LENGTH); LBP3 passes a
// 17-byte buffer. Cut at a UTF-8 character boundary, never inside a character.
constexpr size_t kMaxUserName = 16;

int PS4_SYSV_ABI sceUserServiceGetUserName(int32_t userId, char* name, size_t size) {
    if (!name || size == 0) return kErrorInvalidArgument;
    std::string s;
    if (userId >= 2 && userId <= kMaxLoginUsers) {  // co-op guests
        s = "Player " + std::to_string(userId);
    } else {
        // The Windows account name makes a friendly default for player 1.
        wchar_t wide[257] = {};
        DWORD len = 257;
        if (GetUserNameW(wide, &len) && wide[0]) {
            char utf8[1024];
            const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof(utf8), nullptr, nullptr);
            if (n > 1) s.assign(utf8, static_cast<size_t>(n - 1));
        }
        if (s.empty()) s = "Player";
    }
    size_t cut = std::min({s.size(), kMaxUserName, size - 1});
    while (cut > 0 && cut < s.size() && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    std::memcpy(name, s.data(), cut);
    name[cut] = '\0';
    return kernel::SCE_OK;
}

}  // namespace hle::user

namespace hle::system {
namespace {

enum ParamId : int32_t {
    kParamLang = 1, kParamDateFormat = 2, kParamTimeFormat = 3, kParamTimeZone = 4,
    kParamSummertime = 5, kParamGameParentalLevel = 7, kParamEnterButtonAssign = 1000,
};
constexpr int kErrorParameter = static_cast<int>(0x80A10003);
constexpr int32_t kLangEnglishUs = 1;
constexpr int32_t kEnterButtonCross = 1;

// The system language (SCE_SYSTEM_PARAM_LANG_*), from LBP3_LANGUAGE: a number or a code.
struct Lang { int32_t id; const char* code; const char* name; };
constexpr Lang kLangs[] = {
    {0, "ja", "Japanese"},          {1, "en", "English (US)"},       {2, "fr", "French"},
    {3, "es", "Spanish"},           {4, "de", "German"},             {5, "it", "Italian"},
    {6, "nl", "Dutch"},             {7, "pt", "Portuguese"},         {8, "ru", "Russian"},
    {9, "ko", "Korean"},            {10, "zh-tw", "Chinese (Traditional)"}, {11, "zh-cn", "Chinese (Simplified)"},
    {12, "fi", "Finnish"},          {13, "sv", "Swedish"},           {14, "da", "Danish"},
    {15, "no", "Norwegian"},        {16, "pl", "Polish"},            {17, "pt-br", "Portuguese (Brazil)"},
    {18, "en-gb", "English (UK)"},  {19, "tr", "Turkish"},           {20, "es-la", "Spanish (Latin America)"},
    {21, "ar", "Arabic"},           {22, "fr-ca", "French (Canada)"}, {23, "cs", "Czech"},
    {24, "hu", "Hungarian"},        {25, "el", "Greek"},             {26, "ro", "Romanian"},
    {27, "th", "Thai"},             {28, "vi", "Vietnamese"},        {29, "id", "Indonesian"},
};

}  // namespace

int32_t ParseLanguage(const char* v) {
    if (!v || !*v) return kLangEnglishUs;
    std::string s(v);
    for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c == '_' ? '-' : c);
    if (s == "en-us") return 1;
    if (s == "es-419") return 20;
    bool digits = true;
    for (char c : s) digits = digits && c >= '0' && c <= '9';
    if (digits && s.size() <= 2) {
        const int n = std::atoi(s.c_str());
        for (const Lang& l : kLangs) if (l.id == n) return n;
        return -1;
    }
    for (const Lang& l : kLangs) if (s == l.code) return l.id;
    return -1;
}

const char* LanguageName(int32_t id) {
    for (const Lang& l : kLangs) if (l.id == id) return l.name;
    return "?";
}

int32_t SystemLanguage() {
    static const int32_t lang = [] {
        const char* v = std::getenv("LBP3_LANGUAGE");
        const int32_t id = ParseLanguage(v);
        if (id < 0) {
            std::fprintf(stderr, "[system] LBP3_LANGUAGE=%s: unknown; using English (US)\n", v);
            return kLangEnglishUs;
        }
        if (v && *v)
            std::fprintf(stderr, "[system] system language %d (%s)%s\n", id, LanguageName(id),
                         std::getenv("LBP3_LANGUAGES") ? "" : " - the US edition keeps only English and French unless LBP3_LANGUAGES=all");
        return id;
    }();
    return lang;
}

struct SystemServiceStatus {
    int32_t eventNum;
    bool isSystemUiOverlaid;
    bool isInBackgroundExecution;
    bool isCpuMode7CpuNormal;
    bool isGameLiveStreamingOnAir;
    bool isOutOfVrPlayArea;
    uint8_t reserved[125];
};

struct DisplaySafeAreaInfo { float ratio; uint8_t reserved[128]; };

int PS4_SYSV_ABI sceSystemServiceGetStatus(SystemServiceStatus* status) {
    if (!status) return kErrorParameter;
    std::memset(status, 0, sizeof(*status));
    status->isCpuMode7CpuNormal = true;
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceSystemServiceGetDisplaySafeAreaInfo(DisplaySafeAreaInfo* info) {
    if (!info) return kErrorParameter;
    std::memset(info, 0, sizeof(*info));
    info->ratio = 1.0f;  // no overscan on a PC monitor
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceSystemServiceParamGetInt(int32_t id, int32_t* value) {
    if (!value) return kErrorParameter;
    switch (id) {
    case kParamLang: *value = SystemLanguage(); break;
    case kParamDateFormat: *value = 0; break;  // YYYY/MM/DD
    case kParamTimeFormat: *value = 0; break;  // 12 hour
    case kParamTimeZone: {
        TIME_ZONE_INFORMATION tz;
        GetTimeZoneInformation(&tz);
        *value = -static_cast<int32_t>(tz.Bias);  // minutes east of UTC
        break;
    }
    case kParamSummertime: *value = 0; break;
    case kParamGameParentalLevel: *value = 0; break;  // no restriction
    case kParamEnterButtonAssign: *value = kEnterButtonCross; break;
    default:
        std::fprintf(stderr, "[system] TODO: ParamGetInt(%d)\n", id);
        *value = 0;
    }
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceSystemServiceHideSplashScreen() { return kernel::SCE_OK; }

}  // namespace hle::system

namespace hle {

void RegisterSystemServices(core::Linker& linker) {
    using namespace user;
    using namespace system;
    const char* u = "libSceUserService";
    HLE_REG(linker, u, sceUserServiceInitialize);
    HLE_REG(linker, u, sceUserServiceTerminate);
    HLE_REG(linker, u, sceUserServiceGetInitialUser);
    HLE_REG(linker, u, sceUserServiceGetLoginUserIdList);
    HLE_REG(linker, u, sceUserServiceGetEvent);
    HLE_REG(linker, u, sceUserServiceGetUserName);
    const char* s = "libSceSystemService";
    HLE_REG(linker, s, sceSystemServiceGetStatus);
    HLE_REG(linker, s, sceSystemServiceGetDisplaySafeAreaInfo);
    HLE_REG(linker, s, sceSystemServiceParamGetInt);
    HLE_REG(linker, s, sceSystemServiceHideSplashScreen);
}

}  // namespace hle
