// Quitting from inside the game. See quit.h.
#include "hle/quit.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "core/native_trace.h"
#include "core/runner.h"
#include "core/vfs.h"
#include "hle/abi.h"

namespace hle::quit {
namespace {

std::atomic<bool> g_requested{false};
std::atomic<bool> g_closeApproved{false};  // the save-first quit has finished: the window may close

// eboot.bin offsets (CUSA00473 01.28)
constexpr uint64_t kTranslateNative = 0x839aa0;  // Translate__i: text lookup, then the script string
constexpr uint64_t kMakeScriptString = 0x92b4a0; // (ret, self, const char16_t** text): what the native does with the text

// The profile save (traced 2026-11-20). Players sit in 7 slots of 16 bytes at kSlots: an int32
// user id, then at +8 the player's little profile (littlefart), whose big profile (bigfart) is at
// +0x6a8. A profile's flags byte is at +0x48: 0x01 changed, 0x40 Pod changed, 0x80 (also counted
// as unsaved by the game), 0x02 save requested, 0x04 being saved. ProcessDirty__ sets 0x02 on every
// profile with any of 0xc1; the per-frame save update then queues it on the save manager (whose
// queue count is at +0x30). The three code checks make sure this is the game version these
// offsets are from; anything else is left alone.
constexpr uint64_t kProcessDirty = 0x4f30c0;  // ProcessDirty__
constexpr uint64_t kSlotGetter = 0x4cc1e0;    // a slot's profile: lea of kSlots
constexpr uint64_t kBigGetter = 0x264620;     // the big profile: mov rax,[rdi+0x6a8]; ret
constexpr uint8_t kProcessDirtyCode[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x31, 0xdb, 0x0f, 0x1f, 0x80,
                                         0x00, 0x00, 0x00, 0x00, 0x89, 0xdf, 0xe8, 0x09, 0x91, 0xfd, 0xff};
constexpr uint8_t kSlotGetterCode[] = {0x48, 0x63, 0xcf, 0x48, 0xc1, 0xe1, 0x04, 0x48, 0x8d, 0x05, 0x02, 0x88, 0xed, 0x00};
constexpr uint8_t kBigGetterCode[] = {0x48, 0x8b, 0x87, 0xa8, 0x06, 0x00, 0x00, 0xc3};
constexpr uint64_t kSlots = 0x13a49f0;
constexpr int kSlotCount = 7;
constexpr uint64_t kBigProfile = 0x6a8;
constexpr uint64_t kFlags = 0x48;
constexpr uint8_t kUnsaved = 0xc1, kRequested = 0x02, kSaving = 0x04;
constexpr uint64_t kSaveManager = 0x13a4e50;  // -> the save manager
constexpr uint64_t kQueueCount = 0x30;
constexpr uint64_t kFrameCounter = 0x1202b54;  // int32, one per game frame

core::ScriptNative g_translate = nullptr;
std::atomic<uint64_t> g_armedUntil{0};

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool AnySaveMounted() {
    for (int i = 0; i < 16; ++i)
        if (core::vfs::IsMounted("/savedata" + std::to_string(i))) return true;
    return false;
}

bool Recognised(const uint8_t* base) {
    return std::memcmp(base + kProcessDirty, kProcessDirtyCode, sizeof(kProcessDirtyCode)) == 0 &&
           std::memcmp(base + kSlotGetter, kSlotGetterCode, sizeof(kSlotGetterCode)) == 0 &&
           std::memcmp(base + kBigGetter, kBigGetterCode, sizeof(kBigGetterCode)) == 0;
}

template <class T>
T Read(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

// The flags byte of each signed-in player's little and big profile (the game's own pointers).
template <class F>
void ForEachProfileFlags(uint8_t* base, F f) {
    for (int i = 0; i < kSlotCount; ++i) {
        const uint8_t* slot = base + kSlots + i * 0x10;
        const auto little = Read<uint64_t>(slot + 8);
        if (!little || Read<int32_t>(slot) == 0) continue;
        f(reinterpret_cast<uint8_t*>(little) + kFlags);
        if (const auto big = Read<uint64_t>(reinterpret_cast<uint8_t*>(little) + kBigProfile))
            f(reinterpret_cast<uint8_t*>(big) + kFlags);
    }
}

SaveState SaveStateNow() {
    SaveState s;
    uint8_t* base = core::EbootBase();
    if (!base || !Recognised(base)) return s;
    s.known = true;
    ForEachProfileFlags(base, [&](uint8_t* flags) {
        const uint8_t f = std::atomic_ref<uint8_t>(*flags).load();
        ++s.profiles;
        if (f & kUnsaved) ++s.dirty;
        if (f & (kRequested | kSaving)) ++s.pending;
    });
    if (const auto manager = Read<uint64_t>(base + kSaveManager))
        s.queued = Read<int32_t>(reinterpret_cast<uint8_t*>(manager) + kQueueCount);
    s.frame = Read<int32_t>(base + kFrameCounter);
    s.mounted = AnySaveMounted();
    return s;
}

// What ProcessDirty__ does, as an atomic OR: the game's own changes to the byte are never undone
// (at worst a racing write of the game's drops the request, and the next poll asks again).
void RequestSave() {
    ForEachProfileFlags(core::EbootBase(), [](uint8_t* flags) {
        std::atomic_ref<uint8_t> f(*flags);
        if (f.load() & kUnsaved) f.fetch_or(kRequested);
    });
}

void SaveFirst() {
    const SaveState start = SaveStateNow();
    const uint64_t t0 = NowMs();
    uint64_t frameMs = t0;
    int64_t frame = start.frame;
    bool asked = false;
    for (SaveState now = start;; now = SaveStateNow()) {
        const uint64_t t = NowMs();
        if (now.frame != frame) frame = now.frame, frameMs = t;
        switch (DecideSave(start, now, t - t0, t - frameMs)) {
        case SaveStep::Unknown:
            std::fprintf(stderr, "[quit] not this game version's save data: closing without asking for a save\n");
            return;
        case SaveStep::NothingToSave:
            std::fprintf(stderr, "[quit] nothing unsaved (%d profiles)\n", start.profiles);
            return;
        case SaveStep::Saved:
            std::fprintf(stderr, "[quit] saved first: %d profiles, %llu ms\n", start.profiles,
                         static_cast<unsigned long long>(t - t0));
            return;
        case SaveStep::TimedOut:
        case SaveStep::Stalled:
            std::fprintf(stderr, "[quit] NOT saved: %s (%d unsaved, %d being saved, %d queued, %s)\n",
                         t - t0 >= kSaveTimeoutMs ? "the game didn't finish within 20 s"
                                                   : "the game isn't drawing frames (the attract video?)",
                         now.dirty, now.pending, now.queued, now.mounted ? "save data mounted" : "nothing mounted");
            return;
        case SaveStep::Request:
            if (!asked)
                std::fprintf(stderr, "[quit] saving first: %d of %d profiles have unsaved changes\n", now.dirty,
                             now.profiles);
            asked = true;
            RequestSave();
            break;
        case SaveStep::Wait: break;
        }
        std::fflush(stderr);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void PS4_SYSV_ABI Translate(void* self, void* ret, const unsigned long long* args) {
    const int32_t key = args ? static_cast<int32_t>(args[0] & 0xffffffffu) : 0;
    if (key != kQuitLabel && key != kQuitAction) {
        g_translate(self, ret, args);
        return;
    }
    uint64_t armed = g_armedUntil.load();
    bool quit = false;
    const char16_t* text = PauseMenuText(key, NowMs(), armed, quit);
    g_armedUntil.store(armed);
    if (quit) RequestQuit("the pause menu's Quit Game");
    using Make = void(PS4_SYSV_ABI*)(void* ret, void* self, const char16_t** text);
    reinterpret_cast<Make>(core::EbootBase() + kMakeScriptString)(ret, self, &text);
}

}  // namespace

const char16_t* PauseMenuText(int32_t key, uint64_t nowMs, uint64_t& armedUntil, bool& quit) {
    static const char16_t label[] = u"Quit Game", confirm[] = u"Press again to quit", empty[] = u"";
    quit = false;
    if (key == kQuitLabel) return armedUntil > nowMs ? confirm : label;
    if (key != kQuitAction) return nullptr;
    if (armedUntil <= nowMs) {
        armedUntil = nowMs + 3000;  // first press: arm it for 3 s
    } else {
        quit = true;  // second press within 3 s
    }
    return empty;
}

SaveStep DecideSave(const SaveState& start, const SaveState& now, uint64_t elapsedMs, uint64_t sinceFrameMs) {
    if (!start.known || !now.known) return SaveStep::Unknown;
    if (start.dirty == 0 && start.pending == 0 && start.queued == 0) return SaveStep::NothingToSave;
    if (now.dirty == 0 && now.pending == 0 && now.queued == 0 && !now.mounted && now.frame >= start.frame + 2)
        return SaveStep::Saved;
    if (elapsedMs >= kSaveTimeoutMs) return SaveStep::TimedOut;
    if (sinceFrameMs >= kStallMs) return SaveStep::Stalled;
    if (now.dirty > 0 && now.pending == 0) return SaveStep::Request;
    return SaveStep::Wait;
}

bool QuitRequested() { return g_requested.load(); }

bool CloseWindowNow() {
    if (g_closeApproved.load()) return true;
    if (g_requested.load()) {
        std::fprintf(stderr, "[quit] the window was closed again while saving: closing now\n");
        return true;
    }
    RequestQuit("the window's close button");
    return false;
}

void RequestQuit(const char* why) {
    if (g_requested.exchange(true)) return;
    std::fprintf(stderr, "[quit] %s: saving what isn't saved, then closing once no save data is mounted\n", why);
    std::fflush(stderr);
    std::thread([] {
        SaveFirst();
        int waited = 0;
        while (AnySaveMounted() && waited < 1500) {  // up to 15 s for a save being written
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++waited;
        }
        if (waited) std::fprintf(stderr, "[quit] waited %d ms for save data to be unmounted\n", waited * 10);
        g_closeApproved = true;
        SDL_Event e{};
        e.type = SDL_EVENT_QUIT;
        if (!SDL_PushEvent(&e)) {  // no presenter (a test): end the same way the presenter would
            std::fprintf(stderr, "[video] window closed\n");
            std::fflush(stderr);
            std::_Exit(0);
        }
    }).detach();
}

void InstallPauseMenuQuit() {
    const char* v = std::getenv("LBP3_QUIT_MENU");
    if (!v || !*v || std::string(v) == "0" || std::string(v) == "off") return;
    core::Module* m = core::EbootModule();
    uint8_t* base = core::EbootBase();
    if (!m || !base) return;
    g_translate = core::HookNative(*m, "Translate__i", Translate);
    if (!g_translate) {
        std::fprintf(stderr, "[quit] Translate__i not found: no Quit Game in the pause menu\n");
        return;
    }
    if (reinterpret_cast<uint8_t*>(g_translate) != base + kTranslateNative) {
        // Another game version, or a second Translate__i: put it back rather than guess.
        core::HookNative(*m, "Translate__i", g_translate);
        g_translate = nullptr;
        std::fprintf(stderr, "[quit] Translate__i isn't the expected function: no Quit Game in the pause menu\n");
        return;
    }
    std::fprintf(stderr, "[quit] LBP3_QUIT_MENU: the pause menu's Good Grief! button is Quit Game (press twice)%s\n",
                 std::getenv("LBP3_FILE_EDITS") ? "" : " - but LBP3_FILE_EDITS isn't set, so the menu script is unedited");
}

}  // namespace hle::quit
