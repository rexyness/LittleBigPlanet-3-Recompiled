// Warm-up tour (perf/warmup_step.md). See warmup_tour.h.
//
// What the game provides (eboot.bin, CUSA00473 01.28; decompiled):
// - FUN_007c75d0(): the current level's SlotID (game + 0x2f8; type in the low 32 bits, id in the high).
// - FUN_007c6780(slot, mode) -> FUN_002689c0(game, slot, mode): the general "go to level" behind
//   GoToTutorialLevel__. It looks the slot up in the local user's slot database (FUN_00242af0) and
//   posts a level-change request to the loader; no story-progress check. It does nothing unless
//   three gates hold: FUN_0033efe0(FUN_0033efd0(&DAT_0130d5b8)) < 2, FUN_003500e0(DAT_0130d5e0) == 0
//   and the game's byte at +0xd0 clear (logged by "tour-where"). The game's own launch of a
//   tutorial level passes mode 1 (FUN_0027dc70), so 1 is used for play.
// - FUN_004c9df0(&ref, 1): the story adventure (SlotID type 12, id 349; refcounted: +0x30 counts
//   in units of 0x10000, FUN_0023a550(adv, 0) when the last goes). Its levels are an ordered map at
//   +0x3f8 (MSVC-style tree: header, root at header+8; node: left +0, parent +8, right +0x10,
//   is-nil byte +0x19, key (type, id) at +0x20, slot data at +0x30), the same map FUN_0022b3c0
//   searches for SlotID types 11, 13, 16 and 17.
// - The level object (game + 0x70) is up when its byte +0x45 & 0xfe is 4 (as IsCutSceneActive__'s
//   function checks); IsCutSceneLevel__ (0x7c5d00) and IsStartMenuOpen__ (0x7c3320).
#include "hle/warmup_tour.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "core/runner.h"
#include "hle/abi.h"

namespace hle::warmup_tour {
namespace {

constexpr uint64_t kGame = 0x12ecc80;             // the global game object pointer
constexpr uint64_t kCurrentSlot = 0x7c75d0;       // FUN_007c75d0
constexpr uint64_t kGoToLevel = 0x7c6780;         // FUN_007c6780(slot, mode)
constexpr uint64_t kStoryAdventure = 0x4c9df0;    // FUN_004c9df0(&ref, 1)
constexpr uint64_t kAdventureRelease = 0x23a550;  // FUN_0023a550(adv, 0)
constexpr uint64_t kLocalUser = 0x4cab10;         // FUN_004cab10(): the local user's slot (profile)
constexpr uint64_t kIsCutSceneLevel = 0x7c5d00;   // IsCutSceneLevel__
constexpr uint64_t kIsStartMenuOpen = 0x7c3320;   // IsStartMenuOpen__
constexpr uint64_t kGate1List = 0x130d5b8;        // &DAT_0130d5b8
constexpr uint64_t kGate1Get = 0x33efd0;          // FUN_0033efd0(list, 0)
constexpr uint64_t kGate1Count = 0x33efe0;        // FUN_0033efe0(x) -> count, must be < 2
constexpr uint64_t kGate2Obj = 0x130d5e0;         // DAT_0130d5e0 (a pointer)
constexpr uint64_t kGate2State = 0x3500e0;        // FUN_003500e0(obj) -> must be 0
constexpr uint32_t kPlayMode = 1;

using Clock = std::chrono::steady_clock;

struct Level {
    uint32_t type;
    uint32_t id;
};

enum class Phase { Off, WaitStart, Launch, Loading, Dwell, Settle, Done };

// Commands from the remote-input thread, run by Tick on the game thread.
std::mutex g_cmdMutex;
bool g_cmdList = false, g_cmdWhere = false, g_cmdGoTo = false;
Level g_cmdGoToLevel{};
uint32_t g_cmdGoToMode = kPlayMode;

// The automatic tour (game thread only, except the atomics ApplyInput reads).
bool g_enabled = false;
int g_first = 0, g_last = 1 << 30;
int g_dwellSecs = 150;
std::vector<Level> g_levels;
size_t g_next = 0;
Phase g_phase = Phase::Off;
Clock::time_point g_phaseSince{};
Clock::time_point g_profileSince{};
bool g_profileSeen = false;
int g_launchTries = 0;
uint64_t g_target = 0;
int g_done = 0, g_skipped = 0;
std::atomic<int> g_inputMode{0};  // 0 none, 1 level movement, 2 cutscene taps, 3 start taps
std::atomic<int64_t> g_inputSinceMs{0};

uint8_t* Base() { return core::EbootBase(); }
uint64_t Q(const uint8_t* p, uint64_t off) { return *reinterpret_cast<const uint64_t*>(p + off); }
int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
uint64_t Pack(Level l) { return static_cast<uint64_t>(l.type) | static_cast<uint64_t>(l.id) << 32; }
Level Unpack(uint64_t s) { return {static_cast<uint32_t>(s), static_cast<uint32_t>(s >> 32)}; }

bool Ask(uint64_t off) {
    using Query = bool(PS4_SYSV_ABI*)();
    return reinterpret_cast<Query>(Base() + off)();
}

uint64_t CurrentSlot() {
    using Fn = uint64_t(PS4_SYSV_ABI*)();
    return reinterpret_cast<Fn>(Base() + kCurrentSlot)();
}

bool ProfileLoaded() {
    using Fn = long*(PS4_SYSV_ABI*)();
    long* slot = reinterpret_cast<Fn>(Base() + kLocalUser)();
    return slot && *slot != 0;
}

bool LevelUp() {
    const uint8_t* game = reinterpret_cast<const uint8_t*>(Q(Base(), kGame));
    if (!game) return false;
    const uint8_t* level = reinterpret_cast<const uint8_t*>(Q(game, 0x70));
    return level && (level[0x45] & 0xfe) == 4;
}

struct Gates {
    int count = -1;
    uint32_t loader = ~0u;
    int busyByte = -1;
    bool open() const { return count >= 0 && count < 2 && loader == 0 && busyByte == 0; }
};

Gates ReadGates() {
    Gates g;
    uint8_t* base = Base();
    using Get = uint64_t(PS4_SYSV_ABI*)(void*, int);
    using Count = int(PS4_SYSV_ABI*)(uint64_t);
    using State = uint32_t(PS4_SYSV_ABI*)(uint64_t);
    g.count = reinterpret_cast<Count>(base + kGate1Count)(reinterpret_cast<Get>(base + kGate1Get)(base + kGate1List, 0));
    const uint64_t obj = Q(base, kGate2Obj);
    if (obj) g.loader = reinterpret_cast<State>(base + kGate2State)(obj);
    if (const uint8_t* game = reinterpret_cast<const uint8_t*>(Q(base, kGame))) g.busyByte = game[0xd0];
    return g;
}

void Collect(const uint8_t* node, const uint8_t* header, std::vector<Level>& out, int depth) {
    if (!node || node == header || node[0x19] || depth > 64 || out.size() > 4096) return;
    Collect(reinterpret_cast<const uint8_t*>(Q(node, 0x0)), header, out, depth + 1);
    out.push_back({*reinterpret_cast<const uint32_t*>(node + 0x20), *reinterpret_cast<const uint32_t*>(node + 0x24)});
    Collect(reinterpret_cast<const uint8_t*>(Q(node, 0x10)), header, out, depth + 1);
}

// The story adventure's level map, in key order. Empty if the adventure isn't loaded.
std::vector<Level> StoryLevels() {
    std::vector<Level> out;
    uint8_t* base = Base();
    using Get = long*(PS4_SYSV_ABI*)(long*, char);
    long ref = 0;
    reinterpret_cast<Get>(base + kStoryAdventure)(&ref, 1);
    if (!ref) return out;
    const uint8_t* adv = reinterpret_cast<const uint8_t*>(ref);
    const uint8_t* header = reinterpret_cast<const uint8_t*>(Q(adv, 0x3f8));
    if (header) Collect(reinterpret_cast<const uint8_t*>(Q(header, 0x8)), header, out, 0);
    const long old = InterlockedExchangeAdd(reinterpret_cast<volatile long*>(ref + 0x30), -0x10000);
    if (old == 0x10000) {
        using Release = void(PS4_SYSV_ABI*)(long, int);
        reinterpret_cast<Release>(base + kAdventureRelease)(ref, 0);
    }
    return out;
}

void GoTo(uint64_t slot, uint32_t mode) {
    using Fn = void(PS4_SYSV_ABI*)(uint64_t, uint32_t);
    reinterpret_cast<Fn>(Base() + kGoToLevel)(slot, mode);
}

void LogWhere(const char* why) {
    const Level cur = Unpack(CurrentSlot());
    const Gates g = ReadGates();
    std::fprintf(stderr,
                 "[tour] %s: level (type %u, id 0x%x), up %d, cutscene %d, start menu %d; gates: count %d (<2), "
                 "loader %u (0), busy byte %d (0) -> %s\n",
                 why, cur.type, cur.id, LevelUp(), LevelUp() && Ask(kIsCutSceneLevel), Ask(kIsStartMenuOpen), g.count,
                 g.loader, g.busyByte, g.open() ? "open" : "CLOSED");
}

void SetPhase(Phase p) {
    g_phase = p;
    g_phaseSince = Clock::now();
}

double PhaseSecs() { return std::chrono::duration<double>(Clock::now() - g_phaseSince).count(); }

void SetInput(int mode) {
    g_inputSinceMs = NowMs();
    g_inputMode = mode;
}

void LaunchNext() {
    while (g_next < g_levels.size() && (static_cast<int>(g_next) < g_first || static_cast<int>(g_next) > g_last))
        ++g_next;
    if (g_next >= g_levels.size()) {
        SetInput(0);
        SetPhase(Phase::Done);
        std::fprintf(stderr, "[tour] done: %d level(s) visited, %d skipped\n", g_done, g_skipped);
        std::fflush(stderr);
        return;
    }
    const Level l = g_levels[g_next];
    g_target = Pack(l);
    g_launchTries = 1;
    SetInput(0);
    std::fprintf(stderr, "[tour] %zu/%zu launch (type %u, id 0x%x)\n", g_next, g_levels.size(), l.type, l.id);
    std::fflush(stderr);
    if (!ReadGates().open()) LogWhere("gates before launch");
    GoTo(g_target, kPlayMode);
    SetPhase(Phase::Launch);
}

void Skip(const char* why) {
    const Level l = Unpack(g_target);
    std::fprintf(stderr, "[tour] %zu skip (type %u, id 0x%x): %s\n", g_next, l.type, l.id, why);
    LogWhere("at skip");
    ++g_skipped;
    ++g_next;
    LaunchNext();
}

void RunTour() {
    switch (g_phase) {
    case Phase::Off:
    case Phase::Done:
        return;
    case Phase::WaitStart: {
        // Start once a profile has been loaded for 20 s and a level (the Pod) is up, with no menu open.
        if (!g_profileSeen) {
            if (ProfileLoaded() && LevelUp()) {
                g_profileSeen = true;
                g_profileSince = Clock::now();
            }
            return;
        }
        if (std::chrono::duration<double>(Clock::now() - g_profileSince).count() < 20 || !LevelUp() ||
            Ask(kIsStartMenuOpen))
            return;
        g_levels = StoryLevels();
        std::fprintf(stderr, "[tour] the story adventure lists %zu level(s)\n", g_levels.size());
        for (size_t i = 0; i < g_levels.size(); ++i)
            std::fprintf(stderr, "[tour] level %zu: type %u, id 0x%x\n", i, g_levels[i].type, g_levels[i].id);
        if (g_levels.empty()) {
            std::fprintf(stderr, "[tour] no levels found (the adventure isn't loaded?): retrying in 10 s\n");
            g_profileSince = Clock::now() - std::chrono::seconds(10);
            return;
        }
        g_next = 0;
        LaunchNext();
        return;
    }
    case Phase::Launch: {
        const uint64_t cur = CurrentSlot();
        if (cur == g_target) {
            SetPhase(Phase::Loading);
            return;
        }
        if (PhaseSecs() > 20 * g_launchTries && g_launchTries < 3 && LevelUp()) {
            ++g_launchTries;
            LogWhere("no level change yet, launching again");
            GoTo(g_target, kPlayMode);
        }
        if (PhaseSecs() > 75) Skip("the level never started");
        return;
    }
    case Phase::Loading:
        if (LevelUp() && CurrentSlot() == g_target) {
            if (PhaseSecs() < 3) return;  // let the level settle
            const bool cutscene = Ask(kIsCutSceneLevel);
            const Level l = Unpack(g_target);
            std::fprintf(stderr, "[tour] %zu arrived (type %u, id 0x%x) after %.0f s, %s\n", g_next, l.type, l.id,
                         PhaseSecs(), cutscene ? "cutscene: letting it play" : "level: moving around");
            std::fflush(stderr);
            SetInput(cutscene ? 2 : 1);
            SetPhase(Phase::Dwell);
            return;
        }
        if (PhaseSecs() > 120) Skip("the level never finished loading");
        return;
    case Phase::Dwell: {
        const bool cutscene = g_inputMode == 2;
        const bool left = CurrentSlot() != g_target;
        const double limit = cutscene ? 300.0 : g_dwellSecs;
        if (!left && PhaseSecs() < limit) return;
        const Level l = Unpack(g_target);
        std::fprintf(stderr, "[tour] %zu leave (type %u, id 0x%x) after %.0f s: %s\n", g_next, l.type, l.id,
                     PhaseSecs(), left ? "the game moved on" : "dwell over");
        ++g_done;
        ++g_next;
        SetInput(0);
        // Wait for whatever the game is loading (a cutscene hands over to its next level) before the next launch.
        SetPhase(Phase::Settle);
        return;
    }
    case Phase::Settle: {
        // The next launch once a level has been up for 5 s with the gates open, or after 2 minutes anyway.
        static Clock::time_point upSince{};
        static bool up = false;
        const bool nowUp = LevelUp() && ReadGates().open();
        if (nowUp && !up) upSince = Clock::now();
        up = nowUp;
        if ((up && std::chrono::duration<double>(Clock::now() - upSince).count() >= 5) || PhaseSecs() > 120) {
            up = false;
            LaunchNext();
        }
        return;
    }
    }
}

}  // namespace

Move Pattern(uint64_t ms, bool cutscene) {
    if (cutscene) return {0.f, ms % 4000 < 150};
    const uint64_t t = ms % 10000;
    const float x = t < 5000 ? 1.f : (t < 8000 ? -1.f : 0.f);
    return {x, ms % 1100 < 180};
}

void InstallHooks() {
    const char* v = std::getenv("LBP3_WARMUP_TOUR");
    if (!v || !*v || std::string(v) == "0") return;
    if (std::string(v) != "1") {
        const std::string s(v);
        const size_t dash = s.find('-');
        g_first = std::atoi(s.substr(0, dash).c_str());
        g_last = dash == std::string::npos ? g_first : std::atoi(s.substr(dash + 1).c_str());
    }
    if (const char* d = std::getenv("LBP3_WARMUP_TOUR_DWELL")) g_dwellSecs = std::max(10, std::atoi(d));
    const DWORD attr = GetFileAttributesA("analysis\\savedata_live_is_test");
    if (attr == INVALID_FILE_ATTRIBUTES) {
        std::fprintf(stderr,
                     "[tour] LBP3_WARMUP_TOUR is set but the live saves aren't marked as test saves "
                     "(analysis\\savedata_live_is_test): refusing to tour the human's saves\n");
        return;
    }
    g_enabled = true;
    SetPhase(Phase::WaitStart);
    SetInput(3);
    std::fprintf(stderr, "[tour] warm-up tour on: levels %d-%d, %d s of movement per level\n", g_first,
                 g_last == (1 << 30) ? -1 : g_last, g_dwellSecs);
}

void ApplyInput(ps4::PadData& out) {
    const int mode = g_inputMode.load(std::memory_order_relaxed);
    if (!mode) return;
    const uint64_t ms = static_cast<uint64_t>(NowMs() - g_inputSinceMs.load(std::memory_order_relaxed));
    if (mode == 3) {  // before the tour starts: tap Cross now and then (dismisses the Pod's network notice)
        if (ms % 3000 < 150) out.buttons |= ps4::PAD_CROSS;
        return;
    }
    const Move m = Pattern(ms, mode == 2);
    if (m.cross) out.buttons |= ps4::PAD_CROSS;
    if (mode == 1) out.leftStick.x = static_cast<uint8_t>(std::clamp(128.f + m.stickX * 127.f, 0.f, 255.f));
}

void Tick() {
    if (!Base()) return;
    bool list = false, where = false, goTo = false;
    Level target{};
    uint32_t mode = kPlayMode;
    {
        std::lock_guard lock(g_cmdMutex);
        std::swap(list, g_cmdList);
        std::swap(where, g_cmdWhere);
        std::swap(goTo, g_cmdGoTo);
        target = g_cmdGoToLevel;
        mode = g_cmdGoToMode;
    }
    if (list) {
        const auto levels = StoryLevels();
        std::fprintf(stderr, "[tour] the story adventure lists %zu level(s)\n", levels.size());
        for (size_t i = 0; i < levels.size(); ++i)
            std::fprintf(stderr, "[tour] level %zu: type %u, id 0x%x\n", i, levels[i].type, levels[i].id);
    }
    if (where) LogWhere("where");
    if (goTo) {
        LogWhere("before goto");
        std::fprintf(stderr, "[tour] goto (type %u, id 0x%x), mode %u\n", target.type, target.id, mode);
        GoTo(Pack(target), mode);
    }
    if (g_enabled) RunTour();
}

void Command(const std::string& word, std::istream& in) {
    std::lock_guard lock(g_cmdMutex);
    if (word == "tour-list") {
        g_cmdList = true;
    } else if (word == "tour-where") {
        g_cmdWhere = true;
    } else if (word == "tour-goto") {
        std::string type, id;
        uint32_t mode = kPlayMode;
        in >> type >> id >> mode;
        g_cmdGoToLevel = {static_cast<uint32_t>(std::strtoul(type.c_str(), nullptr, 0)),
                          static_cast<uint32_t>(std::strtoul(id.c_str(), nullptr, 0))};
        g_cmdGoToMode = mode;
        g_cmdGoTo = true;
    } else {
        std::fprintf(stderr, "[tour] unknown command '%s'\n", word.c_str());
    }
}

}  // namespace hle::warmup_tour
