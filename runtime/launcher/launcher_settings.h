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
    std::string res = "2560x1440";  // WxH, one of Resolutions() (older files: res=1440 or 1080)
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
    // display and sound
    std::string display;       // LBP3_DISPLAY: empty = where Windows puts it, "2" = the 2nd display, "165hz"
    std::string window_mode = "windowed";  // LBP3_WINDOW: windowed, borderless or fullscreen
    int volume = 100;          // LBP3_VOLUME (0-100; 100 = unset)
    bool downmix = true;       // the runtime's own 7.1 -> stereo; false = SDL's (LBP3_DOWNMIX=sdl)
    std::string dlc_dir;       // empty = <dump>\CUSA00473-DLC; else LBP3_ADDCONT_DIR
    // online (off; no server is built in). One switch: the levels/profiles server (Refresh,
    // LBP3_ONLINE*) and shadNet (sign-in, friends, multiplayer; LBP3_SHADNET*) together. An empty
    // server or name is taken from user\config\online_account.txt (EffectiveOnline), and the
    // password only ever from that file (the game reads it there itself).
    bool online = false;
    std::string online_server;
    std::string online_name;
    std::string shadnet_server;      // empty = the online server's host (play.lbppc.com -> its shadNet)

    // the launcher itself
    bool setup_done = false;         // the first-run wizard was finished or skipped
    int win_x = -1, win_y = -1;      // the window's last place (-1 = centred on the main display)
    int win_w = 0, win_h = 0;        // and size in pixels (0 = the default)
    bool win_max = false;
    // Discord Rich Presence: opt-in, and only with the player's own application ID (no default:
    // another app's ID would show its name and art).
    bool discord = false;
    std::string discord_app_id;
    std::string pkg_extractor;       // empty = <game dump folder>\pkg_extractor.exe
    std::string update_remote = "fork";  // the git remote "Check for updates" reads (dev checkouts)
    bool update_on_start = false;    // check for a new release (or new commits) when the launcher opens
    bool tray_while_playing = false; // hide to the notification area while the game runs
    bool tour_done = false;          // the short tour of the launcher was shown
    bool confirm_quit = true;        // ask before closing the launcher while the game runs
};

// Window modes the runtime knows (LBP3_WINDOW).
bool ValidWindowMode(const std::string& mode);
// LBP3_DISPLAY: a display number (1-16) or a refresh rate ("165hz", "59.94Hz"); empty = none.
bool ValidDisplay(const std::string& display);

// A Discord application ID is a snowflake: 17-20 digits.
bool ValidDiscordAppId(const std::string& id);
// The shadNet server the game is given: shadnet_server, else the online server's host name
// ("https://play.lbppc.com" -> "play.lbppc.com", which the game maps to LBPPC's shadNet).
std::string ShadnetServer(const Settings& s);

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

// The render sizes offered (LBP3_RES), LBPPC's list: 16:9 from 720p to 4K, 21:9 and 32:9. The game
// draws at that size; a size wider than the window's aspect is shown with black bars.
struct Resolution {
    const char* value;  // "WxH"
    const char* name;
};
const std::vector<Resolution>& Resolutions();
// A launcher.ini res value as one of Resolutions()' values: "1440" -> "2560x1440", "1080" ->
// "1920x1080" (the older files), "WxH" when it's in the list; empty otherwise.
std::string CanonicalRes(const std::string& v);

// The language codes the game knows (LBP3_LANGUAGE), with their names, for the dropdown.
struct Language {
    const char* code;
    const char* name;
};
const std::vector<Language>& Languages();

}  // namespace launcher
