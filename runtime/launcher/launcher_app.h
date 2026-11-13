// LBP3Launcher's window: the state shared by its files.
//   launcher_main.cpp    the loop, the game process, the tray, closing, --shot
//   launcher_theme.cpp   the look: colours, fonts, cards, the PLAY button, the status pill, toasts,
//                        the panning art, page transitions, icons
//   launcher_pages.cpp   Home, Game, Graphics (display and sound), Controls (and the remap editor),
//                        DLC, Saves, Controllers, What's new, Tools (updates), Help, the setup and the tour
//   launcher_online.cpp  the Online hub (status, sign-in test, accounts, news), Friends, Community
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <shobjidl.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "input/motion.h"
#include "launcher_accounts.h"
#include "launcher_dump.h"
#include "launcher_friends.h"
#include "launcher_input.h"
#include "launcher_net.h"
#include "launcher_refresh.h"
#include "launcher_release.h"
#include "launcher_settings.h"
#include "launcher_shell.h"
#include "launcher_stats.h"
#include "launcher_sys.h"
#include "launcher_update.h"

namespace launcher::ui {
namespace fs = std::filesystem;

// ---- background work ----
struct Job {
    std::mutex m;
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};
    bool done = false, ok = false;
    std::string message;
    std::vector<std::string> lines;
    float progress = -1.f;  // 0..1 when known
    void Start();
    void Line(const std::string& s);
    void Finish(bool success, const std::string& msg);
};

// ---- the game ----
enum class GameState { Idle, Starting, Running, Ended };
struct Run {
    std::mutex m;
    GameState state = GameState::Idle;
    std::uintptr_t process = 0;
    long exit_code = 0;
    std::string note, error;
    std::int64_t started = 0;
    std::string level;
    std::string discord;
    fs::path log;
};
extern Run g_run;
GameState CurrentState();

// ---- pages ----
enum Page { Home, Community, Online, Friends, GamePage, Graphics, Controls, Dlc, Saves, Controllers, News, Tools, HelpPage, kPages };
extern const char* const kPageNames[kPages];
extern const char* const kPageKeys[kPages];

struct Texture {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};
struct PadView {
    SDL_Gamepad* pad = nullptr;
    SDL_JoystickID id = 0;
    input::motion::Filter filter;
    Uint64 last_ns = 0;
};
struct AccountView {
    bool exists = false;
    AccountFile file;
};
struct Toast {
    std::string text;
    ImVec4 color;
    double born = 0;
};

// The Community page's state.
struct CommunityState {
    std::string category = "teamPicks";
    std::string query;
    char search[128] = {};
    int skip = 0;
    LevelPage page;
    bool loaded = false;
    Job job;
    std::mutex m;
    // icons by hash: PNG bytes from the worker, textures made on the UI thread
    std::map<std::string, std::string> icon_bytes;
    std::map<std::string, Texture> icons;
    std::vector<std::string> icon_queue;
    std::atomic<bool> icon_worker{false};
    int selected = -1;
    Job queue_job;
    std::string token;  // the website sign-in, in memory only
    std::string token_for;  // ... and whose it is (a switch of account signs in again)
};

// The Online hub's state.
struct OnlineState {
    Job ping;
    ServerInfo info;
    std::vector<Announcement> news;
    std::string news_error;
    bool pinged = false;
    Job test;
    std::string test_shadnet, test_web;  // plain words; empty = not tested
    bool test_shadnet_ok = false, test_web_ok = false;
    std::vector<AccountEntry> accounts;
    double accounts_read = -100;
    Job create;
    // modals
    int password_for = -1;   // account index whose saved password is being changed
    char password_buf[80] = {};
    bool password_show = false;
    bool create_open = false;
    char create_name[32] = {};
    char create_pass[80] = {};
    bool show_password = false;
    bool create_toasted = false;
};

// The Friends page's state.
struct FriendsState {
    FriendsSession session;
    std::thread stopper;
    std::atomic<bool> stopping{false};
    char add_name[32] = {};
    std::string confirm_remove;
    double last_game_poll = 0;
};

// The controls editor's state.
struct ControlsState {
    InputConfig config;
    bool loaded = false;
    std::string capture_key;   // the row waiting for a key or pad button ("" = none)
    bool capture_add = false;  // add the key instead of replacing
    std::string captured;      // filled by the event loop
    std::string message;
};

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Settings s;
    std::string saved_ini;
    Paths p;
    bool portable = false;
    std::string version;  // InstalledVersion, read once
    std::vector<Check> checks;
    std::vector<Backup> backups;
    bool game_running = false, marker = false;
    double last_refresh = -10.0;
    AccountView acct;
    Page page = Home;
    Page prev_page = Home;
    double page_since = 0;
    std::vector<Toast> toasts;
    int restore_index = -1;
    bool shot_mode = false;
    std::string shot_server;  // --shot-server: a local mock the pictures may read
    double time = 0;          // the animation clock (fixed steps in --shot)
    float dt = 1.f / 60.f;
    // art from the dump
    Texture art, icon;
    fs::path art_from;
    // setup and tour
    bool wizard = false;
    int wizard_step = 0;
    char wizard_name[32] = {}, wizard_pass[80] = {};
    std::string wizard_dir;  // the game-files step's typed path
    int wizard_online_choice = 0;  // 0 create, 1 have one, 2 not now
    bool tour = false;
    int tour_step = 0;
    std::map<std::string, std::pair<ImVec2, ImVec2>> marks;  // places the tour points at (filled while drawing)
    // closing
    bool confirm_close = false, close_now = false;
    // tray
    bool hidden_to_tray = false;
    // jobs
    Job file_check;
    std::vector<DumpItem> file_results;
    std::mutex file_results_m;
    Job dlc_install;
    std::vector<DlcPack> dlc;
    std::atomic<bool> dlc_dirty{true};
    char dlc_filter[128] = {};
    Job update_check, update_build;
    UpdateInfo update;
    std::mutex update_m;
    Job release_check, release_install;
    ReleaseInfo release;
    std::mutex release_m;
    Job stats_job;
    PlayStats stats;
    std::mutex stats_m;
    std::vector<Change> news;
    std::vector<std::pair<std::string, std::string>> manifest;
    std::vector<std::string> highlights;  // a package's RELEASE-NOTES.md "What's in it" (no git history there)
    bool news_loaded = false;
    std::atomic<bool> news_ready{false};
    std::mutex news_m;
    // controllers
    std::vector<std::unique_ptr<PadView>> pads;
    int pad_sel = 0;
    double circle_held = 0;
    bool pads_allowed = true;
    Job discord_test;
    // pages with their own state
    CommunityState community;
    OnlineState online;
    FriendsState friends;
    ControlsState controls;
};

// ---- shared helpers (launcher_main.cpp / launcher_pages.cpp) ----
void Refresh(App& a, bool now = false);
Settings Eff(const App& a);            // the settings the game gets (the active account decides the name)
std::string OnlineServer(const App& a);  // the community server's URL (Eff's, normalized; --shot: the mock)
bool EssentialsOk(const App& a);
bool GameBusy(const App& a);           // any lbp3.exe, or one starting from here
void ToastMsg(App& a, const std::string& text, ImVec4 color);
void PlayGame(App& a);                 // Play (signs out of Friends first)
void StartFileCheck(App& a);
void StartStats(App& a);
void LoadNews(App& a);
void ClosePads(App& a);
void SyncPads(App& a);
std::string When(std::int64_t t);
std::string Duration(std::int64_t seconds);
std::int64_t NowUnix();
std::string Narrow(const wchar_t* w);
std::vector<std::string> PickPaths(bool folder, bool multi, const wchar_t* title, const COMDLG_FILTERSPEC* spec, UINT nspec);
std::string PickFolder(const wchar_t* title);
Texture MakeTexture(SDL_Renderer* r, const Image& img);
void FreeTexture(Texture& t);
void StopFriends(App& a, bool wait);   // sign out of shadNet (Friends), now or in the background
void StartCreateAccount(App& a, const std::string& name);  // both accounts on play.lbppc.com
bool DevCheckout(const App& a);        // a git checkout (updates by git), not a package
void StartUpdateCheck(App& a);         // git fetch (dev checkouts)
void StartReleaseCheck(App& a);        // the latest GitHub release (packages)

// ---- pages ----
void PageHome(App& a);
void PageCommunity(App& a);
void PageOnline(App& a);
void PageFriends(App& a);
void PageGame(App& a);
void PageGraphics(App& a);
void PageControls(App& a);
void PageDlc(App& a);
void PageSaves(App& a);
void PageControllers(App& a);
void PageNews(App& a);
void PageTools(App& a);
void PageHelp(App& a);
void Wizard(App& a);
void Tour(App& a);
void Modals(App& a);
void OnlineModals(App& a);
void CommunityFrame(App& a);  // icon textures from the worker (UI thread)

}  // namespace launcher::ui
