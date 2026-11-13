// Quitting from inside the game. See quit.h.
#include "hle/quit.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "core/native_trace.h"
#include "core/runner.h"
#include "core/vfs.h"
#include "hle/abi.h"

namespace hle::quit {
namespace {

std::atomic<bool> g_requested{false};

// eboot.bin offsets (CUSA00473 01.28)
constexpr uint64_t kTranslateNative = 0x839aa0;  // Translate__i: text lookup, then the script string
constexpr uint64_t kMakeScriptString = 0x92b4a0; // (ret, self, const char16_t** text): what the native does with the text

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

bool QuitRequested() { return g_requested.load(); }

void RequestQuit(const char* why) {
    if (g_requested.exchange(true)) return;
    std::fprintf(stderr, "[quit] %s: closing the game once no save data is mounted\n", why);
    std::fflush(stderr);
    std::thread([] {
        int waited = 0;
        while (AnySaveMounted() && waited < 1500) {  // up to 15 s for a save being written
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ++waited;
        }
        if (waited) std::fprintf(stderr, "[quit] waited %d ms for save data to be unmounted\n", waited * 10);
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
