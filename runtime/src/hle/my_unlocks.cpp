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
// LBP3_MY_UNLOCKS=lbp1,lbp2 (either or both) runs that import once per launch, on the game's
// main thread, the first time the start menu opens (its IsOnline__ native) with a profile
// loaded. The My Unlocks page itself stays as the game decides (greyed offline).
// LBP3_MY_UNLOCKS_TRACE=1 logs the import screen natives too.
#include "hle/my_unlocks.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/native_trace.h"
#include "core/runner.h"
#include "hle/abi.h"

namespace hle::my_unlocks {
namespace {

// eboot.bin offsets (CUSA00473 01.28)
constexpr uint64_t kActivateImportScreen = 0x836fb0;  // ActivateImportScreen__i (native; ignores self)
constexpr uint64_t kLocalUser = 0x4cab10;             // returns the local player's user/profile slot
constexpr uint64_t kImportScreenActive = 0x13a5708;   // the import screen object (first byte: active)

constexpr uint32_t kLbp1 = 0x1;
constexpr uint32_t kLbp2Worlds = 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40;

uint32_t g_flags = 0;
std::atomic<bool> g_done{false};
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

void RunImport() {
    using Native = void(PS4_SYSV_ABI*)(void* self, void* ret, const unsigned long long* args);
    uint8_t* base = core::EbootBase();
    const unsigned long long args[2] = {g_flags, 0};
    uint64_t ret = 0;
    std::fprintf(stderr, "[unlocks] running the game's own import (ActivateImportScreen flags 0x%x:%s%s)\n", g_flags,
                 g_flags & kLbp1 ? " LittleBigPlanet" : "",
                 g_flags & kLbp2Worlds ? " LittleBigPlanet 2 (all six story worlds)" : "");
    std::fflush(stderr);
    reinterpret_cast<Native>(base + kActivateImportScreen)(nullptr, &ret, args);
    std::fprintf(stderr, "[unlocks] import started: the import screen is %s\n",
                 ImportScreenActive() ? "showing (the items are added one per frame)" : "NOT active");
}

// The start menu's IsOnline__ (main thread, modal overlay open): the trigger.
void PS4_SYSV_ABI IsOnline(void* self, void* ret, const unsigned long long* args) {
    g_isOnline(self, ret, args);
    if (g_done.load(std::memory_order_relaxed) || !ProfileLoaded()) return;
    if (g_done.exchange(true)) return;
    RunImport();
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

void InstallHooks() {
    const char* v = std::getenv("LBP3_MY_UNLOCKS");
    if (!v || !*v) return;
    core::Module* m = core::EbootModule();
    if (!m) return;
    const std::string s(v);
    if (s.find("lbp1") != std::string::npos || s == "all") g_flags |= kLbp1;
    if (s.find("lbp2") != std::string::npos || s == "all") g_flags |= kLbp2Worlds;
    if (!g_flags) {
        std::fprintf(stderr, "[unlocks] LBP3_MY_UNLOCKS=%s: nothing recognised (use lbp1, lbp2 or both)\n", v);
        return;
    }
    g_isOnline = core::HookNative(*m, "IsOnline__", IsOnline);
    if (!g_isOnline) {
        std::fprintf(stderr, "[unlocks] IsOnline__ not found: no import\n");
        return;
    }
    if (const char* t = std::getenv("LBP3_MY_UNLOCKS_TRACE"); t && *t && *t != '0') {
        g_traced[0] = core::HookNative(*m, kTraced[0], Traced<0>);
        g_traced[1] = core::HookNative(*m, kTraced[1], Traced<1>);
        g_traced[2] = core::HookNative(*m, kTraced[2], Traced<2>);
    }
    std::fprintf(stderr,
                 "[unlocks] LBP3_MY_UNLOCKS=%s: the game's own LBP1/LBP2 import (flags 0x%x) runs once, the first "
                 "time the start menu opens with a profile loaded\n",
                 v, g_flags);
}

}  // namespace hle::my_unlocks
