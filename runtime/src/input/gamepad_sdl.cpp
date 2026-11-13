// SDL3 backend: XInput, DirectInput and generic HID pads, plus DualShock 4 /
// DualSense with motion sensors, touch pad and light bar. Pads are hot-plugged;
// a new controller takes the lowest free player slot (see gamepad.h).
#include "input/gamepad.h"
#include "hle/ime_dialog.h"
#include "input/actions.h"
#include "input/game_state.h"
#include "video/framegen.h"
#include "video/presenter.h"
#include "gpu/renderer.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <thread>
#include <mutex>
#include <sstream>
#include <fstream>
#include <deque>

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace input {
namespace {

SDL_Gamepad* g_pads[kMaxPads] = {};
bool g_virtual[kMaxPads] = {};  // slots held by a virtual controller (test runs); never slot 0
uint32_t g_slotGen[kMaxPads] = {};  // bumped whenever a slot's occupant changes (user service)
// Player 1's controller, once one has been in slot 0: "vendor:product[:serial]". While it is
// away, slot 0 stays free for it (player 1 keeps the keyboard), so another pad, e.g. a remote
// guest's virtual pad, can't become player 1 by connecting in that moment.
std::string g_player1Id;
std::string g_player1Name;
bool g_ready = false;
// One slot reader at a time: RefreshSlots closes and reopens g_pads entries, and the game, the
// system dialogs, the overlays and the user service all look at the slots from their threads.
std::mutex g_slotMutex;

struct ButtonMap { SDL_GamepadButton sdl; uint32_t ps4; };
// Positional mapping: south/east/west/north = Cross/Circle/Square/Triangle on any pad.
constexpr ButtonMap kButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, ps4::PAD_CROSS},
    {SDL_GAMEPAD_BUTTON_EAST, ps4::PAD_CIRCLE},
    {SDL_GAMEPAD_BUTTON_WEST, ps4::PAD_SQUARE},
    {SDL_GAMEPAD_BUTTON_NORTH, ps4::PAD_TRIANGLE},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, ps4::PAD_L1},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, ps4::PAD_R1},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, ps4::PAD_L3},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, ps4::PAD_R3},
    {SDL_GAMEPAD_BUTTON_START, ps4::PAD_OPTIONS},
    {SDL_GAMEPAD_BUTTON_TOUCHPAD, ps4::PAD_TOUCH_PAD},
    {SDL_GAMEPAD_BUTTON_BACK, ps4::PAD_TOUCH_PAD},  // pads without a touch pad
    {SDL_GAMEPAD_BUTTON_DPAD_UP, ps4::PAD_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, ps4::PAD_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, ps4::PAD_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, ps4::PAD_RIGHT},
};

// The PS4 sets the digital L2/R2 bits once the trigger is past a small threshold.
constexpr uint8_t kTriggerDigitalThreshold = 30;
// DualShock 4 touch pad resolution, which the game expects touch coordinates in.
constexpr float kTouchResX = 1920.f, kTouchResY = 943.f;

uint8_t StickAxis(Sint16 v) { return static_cast<uint8_t>((static_cast<int>(v) + 32768) >> 8); }
uint8_t TriggerAxis(Sint16 v) { return static_cast<uint8_t>(v <= 0 ? 0 : v * 255 / 32767); }

bool InUse(SDL_JoystickID id) {
    for (SDL_Gamepad* p : g_pads)
        if (p && SDL_GetGamepadID(p) == id) return true;
    return false;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string PadIdentity(SDL_Gamepad* p) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04x:%04x", SDL_GetGamepadVendor(p), SDL_GetGamepadProduct(p));
    std::string id = buf;
    // A DualSense or DualShock 4 reports its MAC as the serial over USB and Bluetooth alike.
    if (const char* serial = SDL_GetGamepadSerial(p); serial && *serial) id += std::string(":") + serial;
    return id;
}

bool IsPlayStation(SDL_Gamepad* p) {
    switch (SDL_GetGamepadType(p)) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return true;
    default: return false;
    }
}

// LBP3_PLAYER1_PAD=<text>: a controller whose name contains it (any case) is player 1's.
bool PinnedPlayer1(SDL_Gamepad* p) {
    static const std::string pin = [] {
        const char* v = std::getenv("LBP3_PLAYER1_PAD");
        return v ? Lower(v) : std::string();
    }();
    if (pin.empty()) return false;
    const char* name = SDL_GetGamepadName(p);
    return name && Lower(name).find(pin) != std::string::npos;
}

bool PinSet() {
    const char* v = std::getenv("LBP3_PLAYER1_PAD");
    return v && *v;
}

void Place(int slot, SDL_Gamepad* p) {
    g_pads[slot] = p;
    ++g_slotGen[slot];
    SDL_SetGamepadSensorEnabled(p, SDL_SENSOR_ACCEL, true);  // false and harmless without sensors
    SDL_SetGamepadSensorEnabled(p, SDL_SENSOR_GYRO, true);
    const char* name = SDL_GetGamepadName(p);
    if (slot == 0 && g_player1Id.empty()) {
        g_player1Id = PadIdentity(p);
        g_player1Name = name ? name : "controller";
        std::fprintf(stderr, "[input] player 1's controller: %s (%s)\n", g_player1Name.c_str(), g_player1Id.c_str());
    }
    std::fprintf(stderr, "[input] slot %d: %s connected\n", slot, name ? name : "controller");
}

// Under g_slotMutex. Drops unplugged pads and seats newly plugged ones:
// - player 1's own controller (or one LBP3_PLAYER1_PAD names) goes to slot 0;
// - before player 1 has a controller, the first pad to arrive becomes theirs, PlayStation
//   pads first when several arrive together (SDL lists HIDAPI and XInput pads in no fixed
//   order at startup, so an emulated Xbox pad could otherwise come first);
// - any other pad takes the lowest free slot from 1, and slot 0 only if 1..3 are all taken.
void RefreshSlots() {
    for (int s = 0; s < kMaxPads; ++s) {
        SDL_Gamepad*& p = g_pads[s];
        if (p && !SDL_GamepadConnected(p)) {
            std::fprintf(stderr, "[input] slot %d: controller disconnected\n", s);
            SDL_CloseGamepad(p);
            p = nullptr;
            ++g_slotGen[s];
        }
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    struct Fresh { SDL_Gamepad* pad; int rank; };
    std::vector<Fresh> fresh;
    int free = 0;
    for (int s = 0; s < kMaxPads; ++s) free += !g_pads[s] && !g_virtual[s];
    for (int i = 0; i < count && static_cast<int>(fresh.size()) < free; ++i) {
        if (InUse(ids[i])) continue;
        SDL_Gamepad* p = SDL_OpenGamepad(ids[i]);
        if (!p) continue;
        const int rank = PadIdentity(p) == g_player1Id ? 0 : PinnedPlayer1(p) ? 1 : IsPlayStation(p) ? 2 : 3;
        fresh.push_back({p, rank});
    }
    SDL_free(ids);
    std::stable_sort(fresh.begin(), fresh.end(), [](const Fresh& a, const Fresh& b) { return a.rank < b.rank; });
    for (const Fresh& f : fresh) {
        const bool slot0Free = !g_pads[0];
        // Slot 0 is player 1's: their own pad, a pinned one, or the first pad of the session
        // (not one the pin rules out).
        const bool mayTake0 = slot0Free && (f.rank <= 1 || (g_player1Id.empty() && !PinSet()));
        int slot = -1;
        if (mayTake0) {
            slot = 0;
        } else {
            for (int s = 1; s < kMaxPads && slot < 0; ++s)
                if (!g_pads[s] && !g_virtual[s]) slot = s;
            if (slot < 0 && slot0Free) slot = 0;  // every other seat is taken
        }
        if (slot < 0) {
            SDL_CloseGamepad(f.pad);
            continue;
        }
        Place(slot, f.pad);
    }
}

// Under g_slotMutex.
void SetVirtualLocked(int players) {
    int want = std::clamp(players, 1, kMaxPads) - 1;
    for (int s = 1; s < kMaxPads; ++s) {
        if (g_virtual[s] && want > 0) {
            --want;  // keep the ones already placed where they are
        } else if (g_virtual[s]) {
            g_virtual[s] = false;
            ++g_slotGen[s];
            std::fprintf(stderr, "[input] slot %d: virtual pad removed\n", s);
        }
    }
    for (int s = 1; s < kMaxPads && want > 0; ++s) {
        if (g_virtual[s] || g_pads[s]) continue;
        g_virtual[s] = true;
        ++g_slotGen[s];
        --want;
        std::fprintf(stderr, "[input] slot %d: virtual pad added\n", s);
    }
}

}  // namespace

void Init() {
    std::lock_guard lock(g_slotMutex);
    if (g_ready) return;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    g_ready = SDL_Init(SDL_INIT_GAMEPAD);
    if (const char* v = std::getenv("LBP3_VIRTUAL_PADS")) SetVirtualLocked(std::atoi(v));
    if (g_ready) RefreshSlots();
    actions::Init();
}

bool SlotPresent(int slot, uint32_t* generation) {
    if (slot < 0 || slot >= kMaxPads) return false;
    std::lock_guard lock(g_slotMutex);
    if (generation) *generation = g_slotGen[slot];
    return slot == 0 || g_pads[slot] || g_virtual[slot];
}

std::string SlotName(int slot) {
    if (slot < 0 || slot >= kMaxPads) return "nothing";
    std::lock_guard lock(g_slotMutex);
    if (g_pads[slot]) {
        const char* name = SDL_GetGamepadName(g_pads[slot]);
        return name ? name : "controller";
    }
    if (g_virtual[slot]) return "virtual pad";
    return slot == 0 ? "keyboard" : "nothing";
}

void Refresh() {
    std::lock_guard lock(g_slotMutex);
    if (!g_ready) return;
    SDL_UpdateGamepads();
    RefreshSlots();
}

void SetVirtualPlayers(int players) {
    std::lock_guard lock(g_slotMutex);
    SetVirtualLocked(players);
}

bool AutoPressCross(int slot, ps4::PadData& out);

// Test-run remote control (LBP3_REMOTE_INPUT=1 only): every 50 ms the game looks
// for analysis\input_cmd.txt, queues its lines and deletes it. A press or wait line may
// start with a player, "p1".."p4" (default p1), and goes to that player's own queue, so
// players run their sequences side by side: "p2 cross 100", "p3 lstick 1 0 500",
// "p2 wait 300". "pads <n>" sets the number of players with virtual controllers
// (gamepad.h, SetVirtualPlayers). A line is
// "<button>[+<button>...] [ms]" (held that long, default 150), "lstick <x> <y> [ms]"
// or "rstick <x> <y> [ms]" with x/y in -1..1, or "wait <ms>". Buttons: cross circle
// square triangle options touchpad up down left right l1 r1 l2 r2 l3 r3.
// Text entry: "ime <text>" puts <text> (UTF-8, the rest of the line as written) in the
// open text-entry dialog, or the next one to open, in place of what's there and
// confirms it; "ime-cancel" cancels it. Each line handles one dialog.
namespace remote {

struct Command {
    uint32_t buttons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;
    bool left = false, right = false;
    uint64_t ms = 150;
};

std::mutex g_mutex;
std::deque<Command> g_queue[kMaxPads];  // per player slot
uint64_t g_activeUntil[kMaxPads] = {};
Command g_active[kMaxPads];
bool g_hasActive[kMaxPads] = {};

uint32_t ButtonBit(const std::string& name) {
    static const std::pair<const char*, uint32_t> names[] = {
        {"cross", ps4::PAD_CROSS},     {"circle", ps4::PAD_CIRCLE}, {"square", ps4::PAD_SQUARE},
        {"triangle", ps4::PAD_TRIANGLE}, {"options", ps4::PAD_OPTIONS}, {"touchpad", ps4::PAD_TOUCH_PAD},
        {"up", ps4::PAD_UP},           {"down", ps4::PAD_DOWN},     {"left", ps4::PAD_LEFT},
        {"right", ps4::PAD_RIGHT},     {"l1", ps4::PAD_L1},         {"r1", ps4::PAD_R1},
        {"l2", ps4::PAD_L2},           {"r2", ps4::PAD_R2},         {"l3", ps4::PAD_L3},
        {"r3", ps4::PAD_R3}};
    for (const auto& [n, bit] : names)
        if (name == n) return bit;
    return 0;
}

void Parse(const std::string& line) {
    std::istringstream in(line);
    std::string word;
    if (!(in >> word)) return;
    int slot = 0;
    if (word.size() == 2 && word[0] == 'p' && word[1] >= '1' && word[1] <= '0' + kMaxPads) {
        slot = word[1] - '1';
        if (!(in >> word)) return;
    }
    if (word == "pads") {  // "pads <n>": players 2..n get virtual controllers
        int n = 1;
        in >> n;
        SetVirtualPlayers(n);
        return;
    }
    Command c;
    if (word == "f9" || word == "f10") {  // the debug hotkeys (dump / perf report)
        video::TriggerHotkey(word == "f9" ? SDLK_F9 : SDLK_F10);
        return;
    }
    if (word == "action") {  // "action <name> [ms]": hold a keyboard action (input.ini names)
        std::string name;
        uint64_t ms = 150;
        in >> name >> ms;
        if (!actions::HoldForTest(name, ms)) std::fprintf(stderr, "[remote] unknown action '%s'\n", name.c_str());
        return;
    }
    if (word == "fgstill") {  // "fgstill <name> [count]": write the next generated frames (LBP3_FRAMEGEN=1)
        std::string name;
        int count = 3;
        in >> name >> count;
        video::framegen::RequestStills(name.c_str(), count);
        return;
    }
    if (word == "fgdump") {  // "fgdump <set>": frame-generation input capture (LBP3_FG_DUMP)
        std::string set;
        in >> set;
        gpu::fg::Request(set);
        return;
    }
    if (word == "frames") {  // "frames N": record the next N presented frames
        int n = 0;
        in >> n;
        video::RequestFrameDump(n);
        return;
    }
    if (word == "ime") {  // "ime <text>": type into the text-entry dialog and confirm
        std::string text;
        std::getline(in, text);
        if (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(0, 1);  // the separator
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
        hle::ime_dialog::ScriptConfirm(text);
        return;
    }
    if (word == "ime-cancel") {
        hle::ime_dialog::ScriptCancel();
        return;
    }
    if (word == "key") {  // "key <SDL key name>": a PC key press through the window's events (Insert, Escape...)
        std::string name;
        in >> name;
        const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        if (key == SDLK_UNKNOWN) {
            std::fprintf(stderr, "[remote] unknown key '%s'\n", name.c_str());
            return;
        }
        for (const bool down : {true, false}) {
            SDL_Event e{};
            e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            e.key.key = key;
            e.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            e.key.down = down;
            SDL_PushEvent(&e);
        }
        return;
    }
    if (word == "wait") {
        in >> c.ms;
    } else if (word == "lstick" || word == "rstick") {
        float x = 0, y = 0;
        in >> x >> y;
        (word == "lstick" ? c.lx : c.rx) = x;
        (word == "lstick" ? c.ly : c.ry) = y;
        (word == "lstick" ? c.left : c.right) = true;
        in >> c.ms;
    } else {
        size_t start = 0;
        while (start <= word.size()) {
            const size_t plus = word.find('+', start);
            c.buttons |= ButtonBit(word.substr(start, plus == std::string::npos ? std::string::npos : plus - start));
            if (plus == std::string::npos) break;
            start = plus + 1;
        }
        in >> c.ms;
    }
    std::lock_guard lock(g_mutex);
    g_queue[slot].push_back(c);
}

void Poller() {
    for (;;) {
        Sleep(50);
        std::ifstream f("analysis\\input_cmd.txt");
        if (!f) continue;
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(f, line)) lines.push_back(line);
        f.close();
        DeleteFileA("analysis\\input_cmd.txt");
        for (const auto& l : lines) Parse(l);
    }
}

bool Apply(int slot, ps4::PadData& out) {
    static const bool enabled = [] {
        if (!std::getenv("LBP3_REMOTE_INPUT")) return false;
        std::thread(Poller).detach();
        return true;
    }();
    if (!enabled || slot < 0 || slot >= kMaxPads) return false;
    std::lock_guard lock(g_mutex);
    const uint64_t now = SDL_GetTicks();
    Command& active = g_active[slot];
    if (!g_hasActive[slot] || now >= g_activeUntil[slot]) {
        g_hasActive[slot] = false;
        // A short release between consecutive presses, so repeated buttons register.
        if (g_activeUntil[slot] && now < g_activeUntil[slot] + 60) return true;
        if (!g_queue[slot].empty()) {
            active = g_queue[slot].front();
            g_queue[slot].pop_front();
            g_activeUntil[slot] = now + active.ms;
            g_hasActive[slot] = true;
        }
    }
    if (g_hasActive[slot]) {
        out.buttons |= active.buttons;
        auto axis = [](float v) { return static_cast<uint8_t>(std::clamp(128.f + v * 127.f, 0.f, 255.f)); };
        if (active.left) out.leftStick = {axis(active.lx), axis(active.ly)};
        if (active.right) out.rightStick = {axis(active.rx), axis(active.ry)};
    }
    return true;
}

}  // namespace remote

bool Read(int slot, ps4::PadData& out, bool keyboard) {
    std::lock_guard readLock(g_slotMutex);
    std::memset(&out, 0, sizeof(out));
    out.leftStick = out.rightStick = {128, 128};
    out.orientation = {0.f, 0.f, 0.f, 1.f};
    out.acceleration = {0.f, -1.f, 0.f};  // at rest, 1 g down
    out.timestamp = SDL_GetTicksNS() / 1000;
    if (!g_ready || slot < 0 || slot >= kMaxPads) return false;

    SDL_UpdateGamepads();
    RefreshSlots();
    SDL_Gamepad* p = g_pads[slot];
    if (!p) {
        if (slot == 0) {
            // Player 1 always has the keyboard, so the game sees a player without a pad.
            remote::Apply(0, out);
            AutoPressCross(0, out);
            actions::ApplyToFrame(out, keyboard);
        } else if (g_virtual[slot]) {
            remote::Apply(slot, out);  // a virtual controller: neutral unless a script presses
        } else {
            return false;  // nobody in this slot: disconnected
        }
        out.connected = true;
        out.connectedCount = 1;
        return true;
    }

    for (const auto& b : kButtons)
        if (SDL_GetGamepadButton(p, b.sdl)) out.buttons |= b.ps4;

    out.leftStick = {StickAxis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX)),
                     StickAxis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY))};
    out.rightStick = {StickAxis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTX)),
                      StickAxis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTY))};
    out.analogButtons.l2 = TriggerAxis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    out.analogButtons.r2 = TriggerAxis(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    if (out.analogButtons.l2 > kTriggerDigitalThreshold) out.buttons |= ps4::PAD_L2;
    if (out.analogButtons.r2 > kTriggerDigitalThreshold) out.buttons |= ps4::PAD_R2;

    // TODO: verify axis directions against the game (Sixaxis-controlled objects),
    // and integrate the gyro into `orientation` if LBP3 reads it.
    float v[3];
    if (SDL_GetGamepadSensorData(p, SDL_SENSOR_ACCEL, v, 3))
        out.acceleration = {v[0] / SDL_STANDARD_GRAVITY, v[1] / SDL_STANDARD_GRAVITY, v[2] / SDL_STANDARD_GRAVITY};
    if (SDL_GetGamepadSensorData(p, SDL_SENSOR_GYRO, v, 3))
        out.angularVelocity = {v[0], v[1], v[2]};

    if (SDL_GetNumGamepadTouchpads(p) > 0) {
        for (int f = 0; f < 2; ++f) {
            bool down = false;
            float x = 0.f, y = 0.f;
            if (!SDL_GetGamepadTouchpadFinger(p, 0, f, &down, &x, &y, nullptr) || !down) continue;
            ps4::PadTouch& t = out.touchData.touch[out.touchData.touchNum++];
            t.x = static_cast<uint16_t>(x * (kTouchResX - 1));
            t.y = static_cast<uint16_t>(y * (kTouchResY - 1));
            t.id = static_cast<uint8_t>(f);
        }
    }

    AutoPressCross(slot, out);
    remote::Apply(slot, out);
    if (slot == 0) actions::ApplyToFrame(out, keyboard);  // keyboard works alongside a pad
    out.connected = true;
    out.connectedCount = 1;
    return true;
}

void RunGameHooks(int slot) {
    if (slot != 0) return;
    game_state::CutSceneWatchdog();  // the game reads pads once a frame
    actions::RunGameHooks();
}

// Debug aid for getting past prompts while nothing is drawn yet:
// LBP3_AUTOPRESS taps a button every 2 s on slot 0. Value: a button name
// (cross, circle, square, triangle, options), a list "cross,circle" to take
// turns, or anything else for cross.
bool AutoPressCross(int slot, ps4::PadData& out) {
    static const std::vector<uint32_t> buttons = [] {
        std::vector<uint32_t> list;
        const char* env = std::getenv("LBP3_AUTOPRESS");
        if (!env) return list;
        std::string v = env;
        for (const auto& [name, bit] : {std::pair{"cross", ps4::PAD_CROSS}, std::pair{"circle", ps4::PAD_CIRCLE},
                                        std::pair{"square", ps4::PAD_SQUARE}, std::pair{"triangle", ps4::PAD_TRIANGLE},
                                        std::pair{"options", ps4::PAD_OPTIONS}})
            if (v.find(name) != std::string::npos) list.push_back(bit);
        if (list.empty()) list.push_back(ps4::PAD_CROSS);
        return list;
    }();
    if (buttons.empty() || slot != 0) return false;
    const uint64_t ms = SDL_GetTicks();
    if (ms % 2000 < 150) out.buttons |= buttons[(ms / 2000) % buttons.size()];
    out.connected = true;
    out.connectedCount = 1;
    return true;
}

void SetVibration(int slot, uint8_t largeMotor, uint8_t smallMotor) {
    std::lock_guard lock(g_slotMutex);
    if (slot < 0 || slot >= kMaxPads || !g_pads[slot]) return;
    // The PS4 holds a rumble level until it's changed; SDL needs a duration, so use a long one.
    SDL_RumbleGamepad(g_pads[slot], largeMotor * 257, smallMotor * 257, 10000);
}

void SetLightBar(int slot, uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard lock(g_slotMutex);
    if (slot < 0 || slot >= kMaxPads || !g_pads[slot]) return;
    SDL_SetGamepadLED(g_pads[slot], r, g, b);
}

}  // namespace input
