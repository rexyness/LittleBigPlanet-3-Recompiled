// LBP3Launcher's settings: one per play.bat / play_best.bat switch, stored in
// user\config\launcher.ini, and how they turn into the game's LBP3_* environment variables and
// command line (the same mapping as play_best.bat's). Pure code (no Windows, SDL or ImGui), so the
// test program (tests/launchertest.cpp) checks it directly.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace launcher {

// Which lbp3.exe to start.
enum class Build {
    This,    // the lbp3.exe next to LBP3Launcher.exe (the default)
    Best,    // the "best of both worlds" worktree's build (..\lbp3-wt\best\build\lbp3.exe)
    Port,    // the game folder's own build (<root>\build\lbp3.exe, what play.bat starts)
    Custom,  // exe_path
};

// Which shader cache the game uses (LBP3_SHADER_CACHE_DIR).
enum class CacheChoice {
    Auto,    // its own copy (user\shader_cache_best) for any build but the game folder's own, as play_best.bat
    Own,     // always user\shader_cache_best
    Shared,  // always the normal user\shader_cache (no LBP3_SHADER_CACHE_DIR)
};

struct Settings {
    Build build = Build::This;
    std::string exe_path;      // Build::Custom
    std::string game_root;     // empty = found automatically (the folder with user\ and game_dir.txt)
    CacheChoice cache = CacheChoice::Auto;
    bool minimize_while_playing = true;

    // play.bat's own switches
    bool skip_intro = true;
    bool framegen = true;
    int res = 1440;            // 1440 or 1080
    bool my_unlocks = false;
    // play_best.bat's
    bool bloom_1440 = true;
    bool fg_text = true;
    int status_flips = 30;
    int pipeline_threads = 0;  // 0 = all cores but one (the default); 1 = one at a time
    bool vk_pipeline_cache = true;
    bool menu = true;
    bool stats = false;
    bool quit_menu = true;
    std::string language;      // empty = English
    bool motion_filter = true;
    bool tilt_keys = true;
    bool mouse_touch = false;
    // online (off; no server is built in)
    bool online = false;
    std::string online_server;
    std::string online_name;
};

// The ini text for these settings (with a comment per line) and back. Unknown keys and bad values
// are skipped (the default stays), so an old or hand-edited file never stops the launcher.
std::string ToIni(const Settings& s);
Settings FromIni(const std::string& text);

const char* BuildKey(Build b);
const char* CacheKey(CacheChoice c);

using EnvList = std::vector<std::pair<std::string, std::string>>;

// The LBP3_* variables and the command line arguments for these settings, as play_best.bat sets
// them. Not included here, because they need the file system: LBP3_GAME_DIR, LBP3_SHADER_CACHE_DIR
// and the Quit Game pair (LBP3_FILE_EDITS, LBP3_QUIT_MENU), which only apply once its setup step
// has written user\file_edits.bin (see launcher_sys).
struct LaunchPlan {
    EnvList env;
    std::vector<std::string> args;  // after the exe: "--run" and "--skip-intro"
};
LaunchPlan MapSettings(const Settings& s);

// Online names: 3 to 16 letters, digits, - or _ (the community servers' rule).
bool ValidOnlineName(const std::string& name);
// The one server address a player types ("play.lbppc.com", "https://play.lbppc.com", or for a server
// on this PC "127.0.0.1:10061") as the URL the game and the account calls use: a bare host gets
// https://, a local or host:port address http://. Empty when it isn't an address.
std::string NormalizeServer(const std::string& typed);

// The language codes the game knows (LBP3_LANGUAGE), with their names, for the dropdown.
struct Language {
    const char* code;
    const char* name;
};
const std::vector<Language>& Languages();

}  // namespace launcher
