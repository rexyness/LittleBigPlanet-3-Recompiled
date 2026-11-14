// "My Unlocks" offline (ticket 19). See my_unlocks.h.
//
// LBP3's Settings > My Unlocks page imports the LBP1 and LBP2 items that ship in LBP3's own
// game data. The page is only offered when the LBP servers (gone since 2024) report that the
// account played those games, so it can't be used any more. The import itself is local:
// ActivateImportScreen__i(flags) (eboot+0x836fb0) loads the import screen, queues fixed item
// lists built into the executable (bit 1 = LittleBigPlanet; 2, 4, 8, 0x10, 0x20, 0x40 = the
// six LittleBigPlanet 2 story worlds), and starts the game's "Profile Import Thread"; the
// engine's front-end loop then shows the screen and adds the items to the local player's
// profile one per frame (FUN_008bee30, the profile's own add-item function).
//
// Arming it (the in-game menu's Request, or LBP3_MY_UNLOCKS=lbp1,lbp2 at launch) runs that
// import once, on the game's main thread, at the next call of the start menu's IsOnline__
// native with a profile loaded and no import screen up. The My Unlocks page itself stays as the
// game decides (greyed offline). Unarmed, the hook costs one atomic load per call.
// LBP3_MY_UNLOCKS_TRACE=1 logs the import screen natives and when IsOnline__ is called.
//
// The screen's handler (FUN_008bee30) checks every connected controller each frame and cancels
// the import as soon as one holds the back button (internal bit 0x20 = Circle when the game's
// region flag at eboot+0x1060a20 is set, as on this US build; 0x40 = Cross otherwise). The close
// (FUN_008bec30) then shows "Failed to import objects" instead of "successfully imported" and
// skips the profile's "imported" marker. Pressing Circle to leave the start menu while the
// import runs (test sessions o2-o4 on 2026-11-12) throws it away, so GuardImport holds that
// button off the game until the screen closes (LBP3_MY_UNLOCKS_GUARD=off: the game's way).
#include "hle/my_unlocks.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/native_trace.h"
#include "core/runner.h"
#include "hle/abi.h"
#include "hle/pad_types.h"

namespace hle::my_unlocks {
namespace {

// eboot.bin offsets (CUSA00473 01.28)
constexpr uint64_t kActivateImportScreen = 0x836fb0;  // ActivateImportScreen__i (native; ignores self)
constexpr uint64_t kLocalUser = 0x4cab10;             // returns the local player's user/profile slot
constexpr uint64_t kImportScreenActive = 0x13a5708;   // the import screen object (first byte: active)
constexpr uint64_t kBackIsCircle = 0x1060a20;         // 1: the import's cancel button is Circle (0: Cross)

constexpr uint32_t kLbp1 = 0x1;
constexpr uint32_t kLbp2Worlds = 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40;

bool g_guard = true;
bool g_trace = false;
std::atomic<bool> g_hooked{false};
std::atomic<uint32_t> g_armed{0};       // the flags of the import to start; 0 = nothing armed
std::atomic<bool> g_started{false};     // an import this runtime started (this launch)
std::atomic<bool> g_starting{false};    // inside ActivateImportScreen
std::atomic<bool> g_heldLogged{false};
core::ScriptNative g_isOnline = nullptr;

bool ProfileLoaded() {
    using LocalUser = long*(PS4_SYSV_ABI*)();
    uint8_t* base = core::EbootBase();
    if (!base) return false;
    long* slot = reinterpret_cast<LocalUser>(base + kLocalUser)();
    return slot && *slot != 0;
}

bool ImportScreenActive() {
    uint8_t* base = core::EbootBase();
    return base && *reinterpret_cast<volatile uint8_t*>(base + kImportScreenActive) != 0;
}

void RunImport(uint32_t flags) {
    using Native = void(PS4_SYSV_ABI*)(void* self, void* ret, const unsigned long long* args);
    uint8_t* base = core::EbootBase();
    const unsigned long long args[2] = {flags, 0};
    uint64_t ret = 0;
    std::fprintf(stderr, "[unlocks] running the game's own import (ActivateImportScreen flags 0x%x:%s%s)\n", flags,
                 flags & kLbp1 ? " LittleBigPlanet" : "",
                 flags & kLbp2Worlds ? " LittleBigPlanet 2 (all six story worlds)" : "");
    std::fflush(stderr);
    reinterpret_cast<Native>(base + kActivateImportScreen)(nullptr, &ret, args);
    std::fprintf(stderr, "[unlocks] import started: the import screen is %s\n",
                 ImportScreenActive() ? "showing (the items are added one per frame)" : "NOT active");
}

// LBP3_MY_UNLOCKS_TRACE=1: when IsOnline__ is called (at most one line a second while it is).
void TraceIsOnline() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point t0 = Clock::now();
    static long long lastSecond = -1;
    static unsigned calls = 0;
    ++calls;
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    if (ms / 1000 == lastSecond) return;
    lastSecond = ms / 1000;
    std::fprintf(stderr, "[unlocks] trace IsOnline__ at %.1f s (%u calls since the last line)\n", ms / 1000.0, calls);
    calls = 0;
}

// The start menu's IsOnline__ (main thread, modal overlay open): the trigger.
void PS4_SYSV_ABI IsOnline(void* self, void* ret, const unsigned long long* args) {
    g_isOnline(self, ret, args);
    if (g_trace) TraceIsOnline();
    if (g_armed.load(std::memory_order_relaxed) == 0) return;  // the normal case
    if (!ProfileLoaded() || ImportScreenActive()) return;
    g_starting.store(true);
    const uint32_t flags = g_armed.exchange(0);  // a Cancel in between wins
    if (flags) {
        g_started.store(true);
        RunImport(flags);
    }
    g_starting.store(false);
}

// LBP3_MY_UNLOCKS_TRACE=1: the import screen natives.
constexpr std::array<const char*, 3> kTraced = {"ActivateImportScreen__i", "DeactivateImportScreen__",
                                                "IsImportScreenActive__"};
std::array<core::ScriptNative, kTraced.size()> g_traced{};

template <size_t I>
void PS4_SYSV_ABI Traced(void* self, void* ret, const unsigned long long* args) {
    uint8_t local[16] = {};
    void* slot = ret ? ret : local;
    g_traced[I](self, slot, args);
    static std::atomic<int> last{-1};
    const int result = *static_cast<uint8_t*>(slot);
    if (last.exchange(result) != result || I != 2)
        std::fprintf(stderr, "[unlocks] trace %s self %p arg %lld -> %d\n", kTraced[I], self,
                     I == 0 && args ? static_cast<long long>(args[0] & 0xffffffffu) : -1LL, result);
}

}  // namespace

State Decide(bool hooked, uint32_t armed, bool started, bool starting, bool screenActive) {
    if (!hooked) return State::Unavailable;
    if (starting || (started && screenActive)) return State::Running;
    if (armed) return State::Armed;
    return started ? State::Done : State::Idle;
}

State GetState() {
    const bool hooked = g_hooked.load();
    return Decide(hooked, g_armed.load(), g_started.load(), g_starting.load(), hooked && ImportScreenActive());
}

bool Request() {
    const State s = GetState();
    if (s == State::Unavailable || s == State::Running) return false;
    if (g_armed.exchange(kLbp1 | kLbp2Worlds) == 0)
        std::fprintf(stderr,
                     "[unlocks] armed from the in-game menu: the game's own LBP1/LBP2 import starts the next time "
                     "the start menu opens\n");
    return true;
}

void Cancel() {
    if (g_armed.exchange(0) != 0) std::fprintf(stderr, "[unlocks] disarmed from the in-game menu\n");
}

uint32_t FilterBack(uint32_t buttons, bool importRunning, bool backIsCircle) {
    if (!importRunning) return buttons;
    return buttons & ~static_cast<uint32_t>(backIsCircle ? ps4::PAD_CIRCLE : ps4::PAD_CROSS);
}

uint32_t GuardImport(uint32_t buttons) {
    if (!buttons || !g_guard || !g_started.load(std::memory_order_relaxed)) return buttons;
    if (!ImportScreenActive()) return buttons;
    uint8_t* base = core::EbootBase();
    const bool circle = *reinterpret_cast<volatile uint8_t*>(base + kBackIsCircle) != 0;
    const uint32_t out = FilterBack(buttons, true, circle);
    if (out != buttons && !g_heldLogged.exchange(true))
        std::fprintf(stderr,
                     "[unlocks] %s held off the game while the import runs (it would cancel it: \"Failed to import "
                     "objects\"; LBP3_MY_UNLOCKS_GUARD=off lets it through)\n",
                     circle ? "Circle" : "Cross");
    return out;
}

void InstallHooks() {
    const char* v = std::getenv("LBP3_MY_UNLOCKS");
    const std::string s(v ? v : "");
    if (s == "off" || s == "0") {
        std::fprintf(stderr, "[unlocks] LBP3_MY_UNLOCKS=%s: no hook (the in-game menu can't import)\n", v);
        return;
    }
    core::Module* m = core::EbootModule();
    if (!m) return;
    uint32_t flags = 0;
    if (s.find("lbp1") != std::string::npos || s == "all") flags |= kLbp1;
    if (s.find("lbp2") != std::string::npos || s == "all") flags |= kLbp2Worlds;
    if (!s.empty() && !flags)
        std::fprintf(stderr, "[unlocks] LBP3_MY_UNLOCKS=%s: nothing recognised (use lbp1, lbp2 or both)\n", v);
    g_isOnline = core::HookNative(*m, "IsOnline__", IsOnline);
    if (!g_isOnline) {
        std::fprintf(stderr, "[unlocks] IsOnline__ not found: no import\n");
        return;
    }
    if (const char* gd = std::getenv("LBP3_MY_UNLOCKS_GUARD"); gd && std::string(gd) == "off") g_guard = false;
    if (const char* t = std::getenv("LBP3_MY_UNLOCKS_TRACE"); t && *t && *t != '0') {
        g_trace = true;
        g_traced[0] = core::HookNative(*m, kTraced[0], Traced<0>);
        g_traced[1] = core::HookNative(*m, kTraced[1], Traced<1>);
        g_traced[2] = core::HookNative(*m, kTraced[2], Traced<2>);
    }
    g_armed.store(flags);
    g_hooked.store(true);
    if (flags)
        std::fprintf(stderr,
                     "[unlocks] LBP3_MY_UNLOCKS=%s: the game's own LBP1/LBP2 import (flags 0x%x) runs once, the first "
                     "time the start menu opens with a profile loaded; the back button is %s while it runs\n",
                     v, flags, g_guard ? "held off" : "left alone (LBP3_MY_UNLOCKS_GUARD=off: it cancels)");
    else
        std::fprintf(stderr, "[unlocks] ready: the in-game menu can arm the LBP1/LBP2 import\n");
}

}  // namespace hle::my_unlocks
