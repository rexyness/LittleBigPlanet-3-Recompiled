// Offline checks for local co-op (ticket 13): the user service's sign-in and sign-out events,
// libScePad for users 2-4, the seating of controllers in player slots, remote input per player,
// the modal gate per slot, NP state notices and identities, user names and per-user save roots.
//
// No real controller is touched: SDL's HIDAPI, XInput, RawInput, WGI, GameInput and
// DirectInput backends are switched off before the pad layer starts, so the checks don't depend
// on what is plugged in (or in use by a running game). "Real" controllers are SDL virtual
// joysticks with a DualSense's or an Xbox 360 pad's USB ids, which SDL seats like any other
// gamepad; the runtime's own test pads (input::SetVirtualPlayers) are checked separately.
// Exit code 0 = all passed.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/vfs.h"
#include "hle/abi.h"
#include "hle/libScePad.h"
#include "hle/modal.h"
#include "hle/savedata.h"
#include "input/gamepad.h"

// The HLE entry points the game calls (defined in hle/system_services.cpp, np_offline.cpp,
// savedata.cpp); declared here as the game sees them.
namespace hle::user {
struct LoginUserIdList { int32_t userId[4]; };
struct UserEvent { int32_t eventType; int32_t userId; };
int PS4_SYSV_ABI sceUserServiceGetLoginUserIdList(LoginUserIdList* list);
int PS4_SYSV_ABI sceUserServiceGetEvent(UserEvent* ev);
int PS4_SYSV_ABI sceUserServiceGetUserName(int32_t userId, char* name, size_t size);
}  // namespace hle::user

namespace hle::np {
struct OnlineId { char data[16]; int8_t term; int8_t dummy[3]; };
struct NpId { OnlineId handle; uint8_t opt[8]; uint8_t reserved[8]; };
using StateCallback = void(PS4_SYSV_ABI*)(int32_t userId, int32_t state, void* npId, void* userdata);
int PS4_SYSV_ABI sceNpRegisterStateCallback(StateCallback cb, void* userdata);
int PS4_SYSV_ABI sceNpCheckCallback();
int PS4_SYSV_ABI sceNpGetNpId(int32_t userId, NpId* out);
int PS4_SYSV_ABI sceNpGetOnlineId(int32_t userId, OnlineId* out);
}  // namespace hle::np

namespace hle::savedata {
struct DirName { char data[32]; };
struct MountPoint { char data[16]; };
struct Mount {
    int32_t userId;
    int32_t pad0;
    const char* titleId;
    const DirName* dirName;
    const void* fingerprint;
    uint64_t blocks;
    uint32_t mountMode;
    uint8_t reserved[32];
};
struct MountResult {
    MountPoint mountPoint;
    uint64_t requiredBlocks;
    uint32_t unused;
    uint32_t mountStatus;
    uint8_t reserved[28];
    int32_t pad0;
};
int PS4_SYSV_ABI sceSaveDataMount(const Mount* mount, MountResult* result);
int PS4_SYSV_ABI sceSaveDataUmount(const MountPoint* point);
}  // namespace hle::savedata

namespace input::remote {
void Parse(const std::string& line);  // gamepad_sdl.cpp: one line of analysis\input_cmd.txt
}

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

using Event = std::pair<int, int>;  // {type (0 login, 1 logout), user}

std::vector<Event> Drain() {
    std::vector<Event> out;
    hle::user::UserEvent ev{};
    for (int i = 0; i < 64 && hle::user::sceUserServiceGetEvent(&ev) == 0; ++i) out.push_back({ev.eventType, ev.userId});
    return out;
}

std::vector<int> LoggedIn() {
    hle::user::LoginUserIdList list{};
    hle::user::sceUserServiceGetLoginUserIdList(&list);
    std::vector<int> users;
    for (int id : list.userId)
        if (id != -1) users.push_back(id);
    return users;
}

std::string Name(int user, size_t size = 17) {
    char buf[64];
    std::memset(buf, 'X', sizeof(buf));
    hle::user::sceUserServiceGetUserName(user, buf, size);
    return buf;
}

// SDL virtual joysticks standing in for real controllers.
SDL_JoystickID Attach(uint16_t vendor, uint16_t product, const char* name) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = vendor;
    desc.product_id = product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = name;
    return SDL_AttachVirtualJoystick(&desc);
}
SDL_JoystickID AttachDualSense() { return Attach(0x054c, 0x0ce6, "DualSense Wireless Controller"); }
SDL_JoystickID AttachXbox() { return Attach(0x045e, 0x028e, "Xbox 360 Controller"); }  // Parsec's default guest pad

std::vector<int> g_npCalls;
void PS4_SYSV_ABI OnNpState(int32_t userId, int32_t, void*, void*) { g_npCalls.push_back(userId); }

void UserServiceChecks() {
    std::printf("user service\n");
    CHECK((Drain() == std::vector<Event>{{0, 1}}));  // user 1, once
    CHECK(Drain().empty());
    CHECK(LoggedIn() == std::vector<int>{1});

    // Test pads: players 2 and 3 sign in, in order; the list follows.
    input::SetVirtualPlayers(3);
    CHECK((Drain() == std::vector<Event>{{0, 2}, {0, 3}}));
    CHECK((LoggedIn() == std::vector<int>{1, 2, 3}));
    CHECK(input::SlotName(1) == "virtual pad");
    input::SetVirtualPlayers(2);  // player 3's pad goes
    CHECK((Drain() == std::vector<Event>{{1, 3}}));
    CHECK((LoggedIn() == std::vector<int>{1, 2}));

    // Names: at most 16 bytes, guests "Player N".
    CHECK(Name(2) == "Player 2");
    CHECK(Name(4) == "Player 4");
    const std::string p1 = Name(1);
    CHECK(!p1.empty() && p1.size() <= 16);
    CHECK(Name(1, 4).size() <= 3);  // a small buffer is respected
}

void PadChecks() {
    std::printf("pads\n");
    // User 2 is signed in (one test pad left from above): the game's two opens on login.
    const int32_t h2 = hle::pad::scePadOpen(2, 0, 0, nullptr);
    CHECK(h2 > 0);
    CHECK(hle::pad::scePadOpen(2, 2, 0, nullptr) < 0);  // the special pad type: refused, skipped
    CHECK(hle::pad::scePadClose(-1) < 0);              // closing that negative handle: an error
    const int32_t h1 = hle::pad::scePadOpen(1, 0, 0, nullptr);
    CHECK(h1 > 0);
    ps4::PadData d{};
    CHECK_EQ(hle::pad::scePadReadState(h2, &d), 0);
    CHECK(d.connected);
    CHECK_EQ(d.buttons, 0u);

    // Remote input with a player prefix reaches that player only.
    input::remote::Parse("p2 cross 400");
    CHECK_EQ(hle::pad::scePadReadState(h2, &d), 0);
    CHECK((d.buttons & ps4::PAD_CROSS) != 0);
    ps4::PadData d1{};
    CHECK_EQ(hle::pad::scePadReadState(h1, &d1), 0);
    CHECK((d1.buttons & ps4::PAD_CROSS) == 0);
    input::remote::Parse("p2 lstick 1 0 400");
    SDL_Delay(470);  // the cross ends, then a short release, then the stick
    hle::pad::scePadReadState(h2, &d);
    SDL_Delay(80);
    hle::pad::scePadReadState(h2, &d);
    CHECK(d.leftStick.x > 200);
    CHECK((d.buttons & ps4::PAD_CROSS) == 0);
    // "pads <n>" from a script: player 2's test pad goes; the open handle reads disconnected.
    input::remote::Parse("pads 1");
    CHECK((Drain() == std::vector<Event>{{1, 2}}));
    CHECK_EQ(hle::pad::scePadReadState(h2, &d), 0);
    CHECK(!d.connected);
    CHECK_EQ(hle::pad::scePadClose(h2), 0);  // the game closes it on the logout
    CHECK_EQ(hle::pad::scePadReadState(h2, &d) < 0, true);
    input::remote::Parse("pads 2");  // back: login, and the slot can be opened again
    CHECK((Drain() == std::vector<Event>{{0, 2}}));
    const int32_t h2b = hle::pad::scePadOpen(2, 0, 0, nullptr);
    CHECK(h2b > 0);
    CHECK_EQ(hle::pad::scePadReadState(h2b, &d), 0);
    CHECK(d.connected);
    hle::pad::scePadClose(h2b);
    input::SetVirtualPlayers(1);
    CHECK((Drain() == std::vector<Event>{{1, 2}}));
    // An empty slot reads disconnected even with remote input on (no phantom pads).
    const int32_t h3 = hle::pad::scePadOpen(3, 0, 0, nullptr);
    CHECK(h3 > 0);
    input::remote::Parse("p3 cross 200");
    CHECK_EQ(hle::pad::scePadReadState(h3, &d), 0);
    CHECK(!d.connected);
    hle::pad::scePadClose(h3);
    hle::pad::scePadClose(h1);
}

void SeatingChecks() {
    std::printf("seating\n");
    // A DualSense and a Parsec-style Xbox pad that SDL finds together, the Xbox pad listed
    // first: the DualSense is player 1's, the Xbox pad player 2's.
    const SDL_JoystickID guest = AttachXbox();
    const SDL_JoystickID ds = AttachDualSense();
    CHECK(guest != 0 && ds != 0);
    CHECK((Drain() == std::vector<Event>{{0, 2}}));
    CHECK(input::SlotName(0) == "DualSense Wireless Controller");
    CHECK(input::SlotName(1) == "Xbox 360 Controller");

    // The guest drops out and comes back as a new device: out, then in, in slot 1 again.
    SDL_DetachVirtualJoystick(guest);
    CHECK((Drain() == std::vector<Event>{{1, 2}}));
    const SDL_JoystickID guest2 = AttachXbox();
    CHECK((Drain() == std::vector<Event>{{0, 2}}));
    CHECK(input::SlotName(1) == "Xbox 360 Controller");
    // ... and when that happens between two looks, it still shows as out and in.
    SDL_DetachVirtualJoystick(guest2);
    const SDL_JoystickID guest3 = AttachXbox();
    CHECK((Drain() == std::vector<Event>{{1, 2}, {0, 2}}));

    // Player 1's DualSense goes to sleep: slot 0 is kept for it; a second guest who joins
    // meanwhile is player 3, not player 1. The DualSense comes back to slot 0.
    SDL_DetachVirtualJoystick(ds);
    CHECK(Drain().empty());  // player 1 stays signed in (the keyboard)
    CHECK(input::SlotName(0) == "keyboard");
    const SDL_JoystickID guest4 = AttachXbox();
    CHECK((Drain() == std::vector<Event>{{0, 3}}));
    CHECK(input::SlotName(0) == "keyboard");
    const SDL_JoystickID ds2 = AttachDualSense();
    CHECK(Drain().empty());
    CHECK(input::SlotName(0) == "DualSense Wireless Controller");
    CHECK((LoggedIn() == std::vector<int>{1, 2, 3}));

    // Rumble and the light bar on an Xbox pad (no light bar): harmless.
    const int32_t h2 = hle::pad::scePadOpen(2, 0, 0, nullptr);
    ps4::PadLightBarParam light{255, 0, 0, 0};
    ps4::PadVibrationParam rumble{128, 64};
    CHECK_EQ(hle::pad::scePadSetLightBar(h2, &light), 0);
    CHECK_EQ(hle::pad::scePadSetVibration(h2, &rumble), 0);
    ps4::PadData d{};
    CHECK_EQ(hle::pad::scePadReadState(h2, &d), 0);
    CHECK(d.connected);
    CHECK_EQ(d.touchData.touchNum, 0);
    hle::pad::scePadClose(h2);

    SDL_DetachVirtualJoystick(guest3);
    SDL_DetachVirtualJoystick(guest4);
    SDL_DetachVirtualJoystick(ds2);
    CHECK((Drain() == std::vector<Event>{{1, 2}, {1, 3}}));
    CHECK((LoggedIn() == std::vector<int>{1}));
}

void ModalChecks() {
    std::printf("modal gate\n");
    hle::modal::Begin("test");
    CHECK(hle::modal::HoldsPad(0, 0));
    CHECK(hle::modal::HoldsPad(0, 1));  // every player's pad is neutral while a dialog is up
    hle::modal::End();
    // Player 1 still holds the Cross that answered; player 2's reads don't end that wait.
    CHECK(hle::modal::HoldsPad(ps4::PAD_CROSS, 0));
    CHECK(!hle::modal::HoldsPad(0, 1));
    CHECK(!hle::modal::HoldsPad(ps4::PAD_CROSS, 1));  // and player 2 isn't held back
    CHECK(hle::modal::HoldsPad(ps4::PAD_CROSS, 0));
    CHECK(!hle::modal::HoldsPad(0, 0));
}

void NpChecks() {
    std::printf("np\n");
    // User 1's identity as before; a guest has none (signed out) unless LBP3_COOP_NPID=distinct.
    hle::np::NpId id{};
    CHECK_EQ(hle::np::sceNpGetNpId(1, &id), 0);
    CHECK(std::string(id.handle.data, strnlen(id.handle.data, 16)) == "OfflinePlayer");
    CHECK_EQ(hle::np::sceNpGetNpId(2, &id), static_cast<int>(0x80550006));
    hle::np::OnlineId oid{};
    CHECK_EQ(hle::np::sceNpGetOnlineId(3, &oid), static_cast<int>(0x80550006));

    // State notices: user 1 once the callback is registered, then each guest who signs in.
    CHECK_EQ(hle::np::sceNpRegisterStateCallback(OnNpState, nullptr), 0);
    hle::np::sceNpCheckCallback();
    CHECK((g_npCalls == std::vector<int>{1}));
    input::SetVirtualPlayers(2);
    Drain();
    hle::np::sceNpCheckCallback();
    CHECK((g_npCalls == std::vector<int>{1, 2}));
    hle::np::sceNpCheckCallback();
    CHECK((g_npCalls == std::vector<int>{1, 2}));  // once each
    input::SetVirtualPlayers(1);
    Drain();
}

void SaveChecks() {
    std::printf("saves\n");
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("cooptest_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::create_directories(root / "user", ec);
    core::vfs::Init({root / "app0"}, root / "user");
    CHECK(hle::savedata::SaveRoot(1, nullptr) == root / "user" / "savedata" / "CUSA00473");
    CHECK(hle::savedata::SaveRoot(2, "CUSA00473") == root / "user" / "savedata" / "user2" / "CUSA00473");
    hle::savedata::DirName dir{};
    std::snprintf(dir.data, sizeof(dir.data), "LBPxSAVE");
    hle::savedata::Mount m{};
    m.userId = 2;
    m.titleId = "CUSA00473";
    m.dirName = &dir;
    m.blocks = 16;
    m.mountMode = 2 | 32;  // read-write, create if missing
    hle::savedata::MountResult r{};
    CHECK_EQ(hle::savedata::sceSaveDataMount(&m, &r), 0);
    CHECK(fs::is_directory(root / "user" / "savedata" / "user2" / "CUSA00473" / "LBPxSAVE", ec));
    CHECK(!fs::exists(root / "user" / "savedata" / "CUSA00473" / "LBPxSAVE", ec));  // player 1's untouched
    hle::savedata::sceSaveDataUmount(&r.mountPoint);
    fs::remove_all(root, ec);
}

}  // namespace

int main() {
    // No real controllers: only SDL's virtual joysticks and the runtime's test pads.
    for (const char* hint : {"SDL_JOYSTICK_HIDAPI", "SDL_JOYSTICK_RAWINPUT", "SDL_JOYSTICK_WGI", "SDL_XINPUT_ENABLED",
                             "SDL_JOYSTICK_DIRECTINPUT", "SDL_JOYSTICK_GAMEINPUT", "SDL_JOYSTICK_MFI"})
        SDL_SetHint(hint, "0");
    SetEnvironmentVariableA("LBP3_REMOTE_INPUT", "1");
    _putenv("LBP3_REMOTE_INPUT=1");
    CHECK_EQ(hle::pad::scePadInit(), 0);

    UserServiceChecks();
    PadChecks();
    SeatingChecks();
    ModalChecks();
    NpChecks();
    SaveChecks();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
