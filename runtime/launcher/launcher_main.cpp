// LBP3Launcher: the window for starting the native LittleBigPlanet 3 with its settings, and for
// everything around it: the first-run setup, the game file check, DLC, saves, updates, Discord,
// a controller tester, play stats and shortcuts. SDL3 + Dear ImGui, in the in-game menu's colours
// (ui/menu_overlay.cpp), with the game's own art read from the player's dump at run time. Our own
// code; LBPPC's launcher was only an idea (its code is GPL-3.0 and not reused here).
//
//   LBP3Launcher.exe                       the window
//   LBP3Launcher.exe --shot DIR            draws every page (and the setup wizard) into
//                                          DIR\launcher_<page>.bmp on a secondary display, without
//                                          taking the focus, and exits. Nothing is started or sent.
//   LBP3Launcher.exe --print-launch FILE   what Play would start with the saved settings
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "input/motion.h"
#include "launcher_account.h"
#include "launcher_discord.h"
#include "launcher_dump.h"
#include "launcher_proc.h"
#include "launcher_settings.h"
#include "launcher_shell.h"
#include "launcher_stats.h"
#include "launcher_sys.h"
#include "launcher_update.h"

using namespace launcher;

namespace {

// ---------------------------------------------------------------------------------------------
// Look: the in-game menu's palette (ui/menu_overlay.cpp).
const ImVec4 kPanel = ImVec4(18 / 255.f, 22 / 255.f, 34 / 255.f, 1.f);
const ImVec4 kPanel2 = ImVec4(24 / 255.f, 30 / 255.f, 46 / 255.f, 1.f);
const ImVec4 kSide = ImVec4(14 / 255.f, 17 / 255.f, 27 / 255.f, 1.f);
const ImVec4 kEdge = ImVec4(90 / 255.f, 120 / 255.f, 200 / 255.f, 200 / 255.f);
const ImVec4 kSel = ImVec4(52 / 255.f, 96 / 255.f, 196 / 255.f, 235 / 255.f);
const ImVec4 kText = ImVec4(232 / 255.f, 236 / 255.f, 245 / 255.f, 1.f);
const ImVec4 kDim = ImVec4(150 / 255.f, 158 / 255.f, 175 / 255.f, 1.f);
const ImVec4 kAccent = ImVec4(170 / 255.f, 196 / 255.f, 1.f, 1.f);
const ImVec4 kWarn = ImVec4(255 / 255.f, 196 / 255.f, 92 / 255.f, 1.f);
const ImVec4 kGood = ImVec4(120 / 255.f, 214 / 255.f, 140 / 255.f, 1.f);
const ImVec4 kBad = ImVec4(255 / 255.f, 110 / 255.f, 110 / 255.f, 1.f);
const ImVec4 kPlay = ImVec4(46 / 255.f, 150 / 255.f, 86 / 255.f, 1.f);
const ImVec4 kPlayHover = ImVec4(60 / 255.f, 180 / 255.f, 104 / 255.f, 1.f);

ImFont* g_font = nullptr;
ImFont* g_bold = nullptr;
float g_scale = 1.f;  // the display's content scale (1.25 at 125%)

float S(float v) { return v * g_scale; }

void ApplyStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 0.f;
    st.FrameRounding = 6.f;
    st.GrabRounding = 6.f;
    st.TabRounding = 6.f;
    st.PopupRounding = 10.f;
    st.ChildRounding = 10.f;
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    st.WindowPadding = ImVec2(18, 14);
    st.FrameBorderSize = 1.f;
    st.ScrollbarSize = 12.f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = kPanel;
    c[ImGuiCol_ChildBg] = kPanel2;
    c[ImGuiCol_PopupBg] = kPanel2;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
    c[ImGuiCol_Border] = ImVec4(kEdge.x, kEdge.y, kEdge.z, 0.45f);
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_FrameBg] = ImVec4(34 / 255.f, 42 / 255.f, 62 / 255.f, 1.f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(44 / 255.f, 56 / 255.f, 86 / 255.f, 1.f);
    c[ImGuiCol_FrameBgActive] = kSel;
    c[ImGuiCol_CheckMark] = ImVec4(150 / 255.f, 190 / 255.f, 1.f, 1.f);
    c[ImGuiCol_Button] = ImVec4(40 / 255.f, 58 / 255.f, 100 / 255.f, 1.f);
    c[ImGuiCol_ButtonHovered] = kSel;
    c[ImGuiCol_ButtonActive] = ImVec4(70 / 255.f, 120 / 255.f, 220 / 255.f, 1.f);
    c[ImGuiCol_Header] = ImVec4(40 / 255.f, 58 / 255.f, 100 / 255.f, 1.f);
    c[ImGuiCol_HeaderHovered] = kSel;
    c[ImGuiCol_HeaderActive] = kSel;
    c[ImGuiCol_SliderGrab] = kSel;
    c[ImGuiCol_SliderGrabActive] = ImVec4(70 / 255.f, 120 / 255.f, 220 / 255.f, 1.f);
    c[ImGuiCol_Separator] = ImVec4(kEdge.x, kEdge.y, kEdge.z, 0.35f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_NavCursor] = ImVec4(1.f, 0.85f, 0.4f, 1.f);
    c[ImGuiCol_PlotHistogram] = kSel;
    st.ScaleAllSizes(scale);
    st.FontSizeBase = 17.f;
    st.FontScaleDpi = scale;
}

// Dimmed text that wraps at the window's edge (ImGui::TextDisabled doesn't).
void Dim(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void Help(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("%s", text);
}

bool Toggle(const char* label, bool* v, const char* tip) {
    const bool changed = ImGui::Checkbox(label, v);
    if (tip) Help(tip);
    return changed;
}

void Heading(const char* text) {
    ImGui::Spacing();
    ImGui::PushFont(g_bold, 18.f);
    ImGui::TextColored(kAccent, "%s", text);
    ImGui::PopFont();
    ImGui::Separator();
}

bool InputString(const char* label, std::string& value, const char* hint = nullptr, float width = 0.f) {
    char buf[1024];
    std::snprintf(buf, sizeof buf, "%s", value.c_str());
    if (width > 0.f) ImGui::SetNextItemWidth(S(width));
    const bool changed = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof buf) : ImGui::InputText(label, buf, sizeof buf);
    if (changed) value = buf;
    return changed;
}

std::string Narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// Windows' own file or folder dialog. Empty when cancelled.
std::vector<std::string> PickPaths(bool folder, bool multi, const wchar_t* title, const COMDLG_FILTERSPEC* spec, UINT nspec) {
    std::vector<std::string> out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0) | (multi ? FOS_ALLOWMULTISELECT : 0));
    dlg->SetTitle(title);
    if (spec && nspec) dlg->SetFileTypes(nspec, spec);
    if (SUCCEEDED(dlg->Show(nullptr))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items))) {
            DWORD n = 0;
            items->GetCount(&n);
            for (DWORD i = 0; i < n; ++i) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &item))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        out.push_back(Narrow(path));
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            items->Release();
        }
    }
    dlg->Release();
    return out;
}
std::string PickFolder(const wchar_t* title) {
    auto v = PickPaths(true, false, title, nullptr, 0);
    return v.empty() ? std::string() : v[0];
}

std::string When(std::int64_t t) {
    if (!t) return "never";
    const std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_s(&tm, &tt);
    char buf[64];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string Duration(std::int64_t seconds) {
    const std::int64_t h = seconds / 3600, m = (seconds % 3600) / 60;
    if (h > 0) return std::to_string(h) + " h " + std::to_string(m) + " min";
    return std::to_string(m) + " min";
}

std::int64_t NowUnix() { return static_cast<std::int64_t>(std::time(nullptr)); }

// ---------------------------------------------------------------------------------------------
// Background work: one generic job with a log, used by the file check, DLC install, updates and
// the stats.
struct Job {
    std::mutex m;
    std::atomic<bool> running{false};
    std::atomic<bool> cancel{false};
    bool done = false, ok = false;
    std::string message;
    std::vector<std::string> lines;
    void Start() {
        std::lock_guard lock(m);
        running = true;
        cancel = false;
        done = ok = false;
        message.clear();
        lines.clear();
    }
    void Line(const std::string& s) {
        std::lock_guard lock(m);
        lines.push_back(s);
        if (lines.size() > 4000) lines.erase(lines.begin(), lines.begin() + 1000);
    }
    void Finish(bool success, const std::string& msg) {
        std::lock_guard lock(m);
        ok = success;
        message = msg;
        done = true;
        running = false;
    }
};

// ---------------------------------------------------------------------------------------------
// The game process.
enum class GameState { Idle, Starting, Running, Ended };
struct Run {
    std::mutex m;
    GameState state = GameState::Idle;
    std::uintptr_t process = 0;
    long exit_code = 0;
    std::string note, error;
    std::int64_t started = 0;
    std::string level;      // the level being played, from the log
    std::string discord;    // the Rich Presence status line
    fs::path log;           // the log being written
};
Run g_run;

void LaunchWorker(Settings s, Paths p) {
    std::string note, error;
    RotateLogs(p);
    EnvList extra;
    if (s.quit_menu) extra = QuitMenuSetup(p, &note);
    const EnvList env = GameEnvironment(s, p, extra);
    const LaunchPlan plan = MapSettings(s);
    std::string summary = "start " + p.exe.string() + " in " + p.root.string() + " with";
    for (const auto& [k, v] : env) summary += " " + k + "=" + (k == "LBP3_ONLINE_NAME" || k == "LBP3_SHADNET_NAME" ? "<name>" : v);
    AppendLauncherLog(p, summary);
    if (!note.empty()) AppendLauncherLog(p, note);
    const std::uintptr_t h = StartGame(p, env, plan.args, &error);
    {
        std::lock_guard lock(g_run.m);
        g_run.note = note;
        g_run.log = p.root / "analysis" / (p.log_base + "_log.txt");
        if (!h) {
            g_run.state = GameState::Ended;
            g_run.exit_code = -1;
            g_run.error = error;
            AppendLauncherLog(p, "start failed: " + error);
            return;
        }
        g_run.process = h;
        g_run.started = NowUnix();
        g_run.level.clear();
        g_run.state = GameState::Running;
    }
    // While it runs: the level from its log, and Discord if the player turned it on.
    LogTail tail(g_run.log);
    DiscordRpc rpc;
    std::string shown_level = "\x01";
    std::int64_t last_try = 0;
    for (;;) {
        {
            // the game's end, noticed here so it works while the launcher is minimised
            std::lock_guard lock(g_run.m);
            if (g_run.state != GameState::Running) break;
            const long c = GameExitCode(g_run.process);
            if (c != -1) {
                CloseGameHandle(g_run.process);
                g_run.process = 0;
                g_run.exit_code = c;
                g_run.state = GameState::Ended;
                AppendLauncherLog(p, "the game ended with code " + std::to_string(c));
                break;
            }
        }
        std::string level;
        for (const auto& line : tail.Poll()) {
            const Level lv = LevelFromLogLine(line);
            if (lv.ok()) level = lv.Text();
        }
        if (!level.empty()) {
            std::lock_guard lock(g_run.m);
            g_run.level = level;
        }
        if (s.discord && ValidDiscordAppId(s.discord_app_id)) {
            std::string current;
            std::int64_t started;
            {
                std::lock_guard lock(g_run.m);
                current = g_run.level;
                started = g_run.started;
            }
            if (!rpc.Connected() && NowUnix() - last_try > 15) {
                last_try = NowUnix();
                rpc.Connect(s.discord_app_id);
                shown_level = "\x01";
            }
            if (rpc.Connected() && current != shown_level) {
                if (rpc.SetActivity("Playing LittleBigPlanet 3", current, started)) shown_level = current;
            }
            std::lock_guard lock(g_run.m);
            g_run.discord = rpc.Connected() ? "Discord: showing " + (current.empty() ? std::string("the game") : current) : "Discord: " + rpc.why();
        }
        for (int i = 0; i < 10; ++i) {  // 2 s, checking for the end every 0.2 s
            Sleep(200);
            std::lock_guard lock(g_run.m);
            if (g_run.state != GameState::Running || GameExitCode(g_run.process) != -1) break;
        }
    }
    if (rpc.Connected()) rpc.Clear();
    rpc.Close();
}

// ---------------------------------------------------------------------------------------------
// Pages
enum Page { Home, GamePage, Graphics, Controls, Online, Dlc, Saves, Controllers, News, Tools, HelpPage, kPages };
const char* kPageNames[kPages] = {"Home", "Game", "Graphics", "Controls", "Online", "DLC", "Saves", "Controllers", "What's new", "Tools", "Help"};
const char* kPageKeys[kPages] = {"home", "game", "graphics", "controls", "online", "dlc", "saves", "controllers", "news", "tools", "help"};

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

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Settings s;
    std::string saved_ini;
    Paths p;
    std::vector<Check> checks;
    std::vector<Backup> backups;
    bool game_running = false, marker = false;
    double last_refresh = -10.0;
    Page page = Home;
    std::string banner;
    ImVec4 banner_color = kDim;
    int restore_index = -1;
    bool confirm_account = false;
    Job account_job;
    std::vector<std::string> account_result;
    bool account_ok = false;
    std::string account_message;
    bool shot_mode = false;
    // art from the dump
    Texture art, icon;
    fs::path art_from;
    // the setup wizard
    bool wizard = false;
    int wizard_step = 0;
    // jobs
    Job file_check;
    std::vector<DumpItem> file_results;
    std::mutex file_results_m;
    Job dlc_install;
    std::vector<DlcPack> dlc;
    double dlc_listed = -100.0;
    char dlc_filter[128] = {};
    Job update_check, update_build;
    UpdateInfo update;
    std::mutex update_m;
    Job stats_job;
    PlayStats stats;
    std::mutex stats_m;
    std::vector<Change> news;
    std::vector<std::pair<std::string, std::string>> manifest;  // feature, switch
    bool news_loaded = false;
    std::atomic<bool> news_ready{false};
    std::mutex news_m;
    // controllers page
    std::vector<std::unique_ptr<PadView>> pads;
    int pad_sel = 0;
    double circle_held = 0;
    bool pads_allowed = true;  // false while the game runs (it owns the pads then)
    // discord test
    Job discord_test;
};

void Banner(App& a, const std::string& text, ImVec4 color) {
    a.banner = text;
    a.banner_color = color;
}

void Refresh(App& a, bool now = false) {
    const double t = ImGui::GetTime();
    if (!now && t - a.last_refresh < 1.5) return;
    a.last_refresh = t;
    a.p = ResolvePaths(a.s);
    a.checks = RunChecks(a.s, a.p);
    a.backups = ListBackups(a.p);
    a.game_running = GameRunning();
    a.marker = TestMarkerSet(a.p);
}

bool EssentialsOk(const App& a) {
    for (const auto& c : a.checks)
        if (!c.ok && (c.what == "lbp3.exe" || c.what == "Game files (the dump)" || c.what == "Game folder (saves, logs)")) return false;
    return true;
}

GameState CurrentState() {
    std::lock_guard lock(g_run.m);
    return g_run.state;
}

// ---- art ----
Texture MakeTexture(SDL_Renderer* r, const Image& img) {
    Texture t;
    if (!img.ok()) return t;
    t.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (!t.tex) return t;
    SDL_UpdateTexture(t.tex, nullptr, img.rgba.data(), img.w * 4);
    SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.tex, SDL_SCALEMODE_LINEAR);
    t.w = img.w;
    t.h = img.h;
    return t;
}

void LoadArt(App& a) {
    const fs::path sys = a.p.game_dir / "CUSA00473" / "sce_sys";
    if (sys == a.art_from) return;
    a.art_from = sys;
    for (Texture* t : {&a.art, &a.icon})
        if (t->tex) SDL_DestroyTexture(t->tex), *t = Texture{};
    std::error_code ec;
    fs::path pic = sys / "pic1.png";
    if (!fs::exists(pic, ec)) pic = sys / "pic0.png";
    a.art = MakeTexture(a.renderer, Downscale(LoadImageFile(pic), 1600, 900));
    a.icon = MakeTexture(a.renderer, Downscale(LoadImageFile(sys / "icon0.png"), 256, 256));
}

ImTextureRef Ref(const Texture& t) { return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t.tex))); }

// ---- jobs ----
void StartFileCheck(App& a) {
    if (a.file_check.running) return;
    a.file_check.Start();
    std::thread([&a, dir = a.p.game_dir] {
        auto items = CheckDump(dir, [&](const std::string& s) { a.file_check.Line("Checking " + s + "..."); }, &a.file_check.cancel);
        int bad = 0, warn = 0;
        for (const auto& it : items) bad += it.level == DumpItem::Bad, warn += it.level == DumpItem::Warn;
        {
            std::lock_guard lock(a.file_results_m);
            a.file_results = std::move(items);
        }
        a.file_check.Finish(bad == 0, bad ? std::to_string(bad) + " problem(s) found" : (warn ? "OK, with " + std::to_string(warn) + " note(s)" : "Everything checked is fine"));
    }).detach();
}

fs::path PkgExtractor(const App& a) {
    if (!a.s.pkg_extractor.empty()) return fs::path(a.s.pkg_extractor);
    return a.p.game_dir / "pkg_extractor.exe";
}

void StartDlcInstall(App& a, std::vector<std::string> pkgs) {
    if (a.dlc_install.running || pkgs.empty()) return;
    a.dlc_install.Start();
    std::thread([&a, pkgs, tool = PkgExtractor(a), dest = a.p.dlc_dir] {
        std::error_code ec;
        if (!fs::exists(tool, ec)) return a.dlc_install.Finish(false, "pkg_extractor.exe not found at " + tool.string() + " (set it on this page)");
        fs::create_directories(dest, ec);
        int ok = 0, skipped = 0, failed = 0;
        for (const auto& pkg : pkgs) {
            if (a.dlc_install.cancel) break;
            const std::wstring wpkg = fs::path(pkg).wstring();
            a.dlc_install.Line(fs::path(pkg).filename().string() + ":");
            // only DLC packages go to the DLC folder (pkg_extractor --check-type exits 103 for a DLC)
            const ProcResult type = RunCapture(Quote(tool.wstring()) + L" " + Quote(wpkg) + L" --check-type", tool.parent_path(), nullptr, &a.dlc_install.cancel, 60000);
            if (type.exit_code != 103) {
                a.dlc_install.Line("  skipped: not a DLC package" + std::string(type.exit_code == 101 ? " (a base game)" : type.exit_code == 102 ? " (a game update)" : ""));
                ++skipped;
                continue;
            }
            const ProcResult r = RunCapture(Quote(tool.wstring()) + L" " + Quote(wpkg) + L" " + Quote(dest.wstring()), tool.parent_path(),
                                            [&](const std::string& l) { if (!l.empty()) a.dlc_install.Line("  " + l); }, &a.dlc_install.cancel, 600000);
            if (r.started && r.exit_code == 0) ++ok;
            else {
                ++failed;
                a.dlc_install.Line("  failed (exit " + std::to_string(r.exit_code) + ") " + r.error);
            }
        }
        a.dlc_install.Finish(failed == 0, std::to_string(ok) + " installed, " + std::to_string(skipped) + " skipped, " + std::to_string(failed) + " failed");
    }).detach();
}

void StartUpdateCheck(App& a) {
    if (a.update_check.running || a.update_build.running) return;
    a.update_check.Start();
    std::thread([&a, wt = a.p.worktree, remote = a.s.update_remote] {
        UpdateInfo u = CheckUpdates(wt, remote, [&](const std::string& l) { a.update_check.Line(l); }, &a.update_check.cancel);
        const bool ok = u.ok;
        const std::string msg = !u.ok ? u.error : u.incoming.empty() ? "Up to date (" + u.branch + " " + u.head + ")" : std::to_string(u.incoming.size()) + " new change(s) on " + u.remote + "/" + u.branch;
        {
            std::lock_guard lock(a.update_m);
            a.update = std::move(u);
        }
        a.update_check.Finish(ok, msg);
    }).detach();
}

void StartUpdateBuild(App& a) {
    if (a.update_build.running || a.update_check.running) return;
    a.update_build.Start();
    std::error_code ec;
    const bool game_only = fs::equivalent(a.p.worktree, LauncherDir().parent_path(), ec);
    std::thread([&a, wt = a.p.worktree, remote = a.s.update_remote, game_only, paths = a.p] {
        std::string error;
        AppendLauncherLog(paths, "update and build in " + wt.string());
        auto playing = [] {
            if (GameRunning()) return true;
            std::lock_guard lock(g_run.m);
            return g_run.state == GameState::Starting || g_run.state == GameState::Running;
        };
        const bool ok = UpdateAndBuild(wt, remote, game_only, playing, [&](const std::string& l) { a.update_build.Line(l); }, &a.update_build.cancel, &error);
        AppendLauncherLog(paths, ok ? "update and build finished" : "update and build failed: " + error);
        a.update_build.Finish(ok, ok ? (game_only ? "Updated, and the game built. This launcher can't rebuild itself while it runs: "
                                                    "close it and run build.bat in the build folder for its own changes."
                                                  : "Updated and built")
                                     : error);
    }).detach();
}

void StartStats(App& a) {
    if (a.stats_job.running || a.p.root.empty()) return;
    a.stats_job.Start();
    std::thread([&a, dir = a.p.root / "analysis"] {
        PlayStats st = ReadPlayStats(dir);
        {
            std::lock_guard lock(a.stats_m);
            a.stats = std::move(st);
        }
        a.stats_job.Finish(true, "");
    }).detach();
}

// The manifest table of best/README.md (planning repo), if it is next to the game folder.
std::vector<std::pair<std::string, std::string>> ReadManifest(const fs::path& root) {
    std::vector<std::pair<std::string, std::string>> out;
    const fs::path f = root.parent_path() / "lbp3-pc" / ".scratch" / "lbp3-port" / "best" / "README.md";
    std::ifstream in(f);
    std::string line;
    bool in_table = false;
    while (std::getline(in, line)) {
        if (line.rfind("## Manifest", 0) == 0) in_table = true;
        else if (in_table && line.rfind("## ", 0) == 0) break;
        if (!in_table || line.rfind("| ", 0) != 0 || line.rfind("| Feature", 0) == 0 || line.rfind("|---", 0) == 0) continue;
        std::vector<std::string> cells;
        size_t at = 1;
        while (at < line.size()) {
            const size_t next = line.find(" | ", at);
            cells.push_back(line.substr(at, (next == std::string::npos ? line.size() : next) - at));
            if (next == std::string::npos) break;
            at = next + 3;
        }
        auto clean = [](std::string s) {
            std::string o;
            for (char c : s)
                if (c != '`' && c != '*') o.push_back(c);
            while (!o.empty() && (o.front() == ' ')) o.erase(o.begin());
            while (!o.empty() && (o.back() == ' ' || o.back() == '|')) o.pop_back();
            return o;
        };
        if (cells.size() >= 4) out.emplace_back(clean(cells[0]), clean(cells[3]));
    }
    return out;
}

void LoadNews(App& a) {
    if (a.news_loaded) return;
    a.news_loaded = true;
    std::thread([&a, wt = a.p.worktree, root = a.p.root] {
        auto changes = RecentChanges(wt, 40);
        auto manifest = ReadManifest(root);
        std::lock_guard lock(a.news_m);
        a.news = std::move(changes);
        a.manifest = std::move(manifest);
        a.news_ready = true;
    }).detach();
}

// ---------------------------------------------------------------------------------------------
// Pages

void PageHome(App& a) {
    PlayStats st;
    {
        std::lock_guard lock(a.stats_m);
        st = a.stats;
    }
    const float w = ImGui::GetContentRegionAvail().x;
    auto card = [&](const char* id, const char* title, const std::string& value, const char* sub) {
        ImGui::BeginChild(id, ImVec2((w - S(30)) / 4, S(96)), ImGuiChildFlags_Borders);
        Dim("%s", title);
        ImGui::PushFont(g_bold, 22.f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopFont();
        if (sub) Dim("%s", sub);
        ImGui::EndChild();
    };
    Heading("Your play");
    card("c1", "Played in total", st.sessions ? Duration(st.seconds) : std::string("-"), st.sessions ? (std::to_string(st.sessions) + " sessions").c_str() : nullptr);
    ImGui::SameLine();
    card("c2", "Last played", st.last_played ? When(st.last_played) : std::string("-"), nullptr);
    ImGui::SameLine();
    std::string level = st.last_level.empty() ? "-" : st.last_level;
    card("c3", "Last level", level, nullptr);
    ImGui::SameLine();
    card("c4", "DLC packs", std::to_string(a.dlc.size()), "license packs installed");

    Heading("Quick settings");
    int r = a.s.res == 1080 ? 1 : 0;
    ImGui::TextUnformatted("Resolution");
    ImGui::SameLine(S(160));
    if (ImGui::RadioButton("1440p", &r, 0)) a.s.res = 1440;
    ImGui::SameLine();
    if (ImGui::RadioButton("1080p", &r, 1)) a.s.res = 1080;
    ImGui::TextUnformatted("Frame generation");
    ImGui::SameLine(S(160));
    ImGui::Checkbox("60 on screen##fg", &a.s.framegen);
    ImGui::TextUnformatted("Online");
    ImGui::SameLine(S(160));
    ImGui::Checkbox("on##online", &a.s.online);
    ImGui::SameLine();
    {
        const Settings eff = EffectiveOnline(a.s, a.p);
        const std::string srv = NormalizeServer(eff.online_server);
        Dim("%s", !a.s.online ? "(off)" : srv.empty() || !ValidOnlineName(eff.online_name) ? "(set the server and name on the Online page)" : (srv + " as " + eff.online_name).c_str());
    }

    bool missing = !EssentialsOk(a);
    if (missing) {
        Heading("Setup");
        ImGui::TextColored(kWarn, "Something the game needs is missing.");
        if (ImGui::Button("Run the setup")) a.wizard = true, a.wizard_step = 0;
    }

    Heading("What's new");
    std::lock_guard lock(a.news_m);
    if (a.news.empty()) Dim("(reading this build's history...)");
    for (size_t i = 0; i < a.news.size() && i < 5; ++i) {
        Dim("%s", a.news[i].when.c_str());
        ImGui::SameLine(S(110));
        ImGui::TextWrapped("%s", a.news[i].subject.c_str());
    }
    if (!a.news.empty() && ImGui::SmallButton("More...")) a.page = News;
}

void PageGame(App& a) {
    Settings& s = a.s;
    Heading("Which game build");
    const char* builds[] = {"This launcher's build (next to LBP3Launcher.exe)", "Best of both worlds (lbp3-wt\\best)",
                            "Port (the game folder's own build, as play.bat)", "Another lbp3.exe..."};
    int b = static_cast<int>(s.build);
    ImGui::SetNextItemWidth(S(520));
    if (ImGui::Combo("##build", &b, builds, IM_ARRAYSIZE(builds))) s.build = static_cast<Build>(b), Refresh(a, true), a.news_loaded = false;
    Help("Which lbp3.exe to start. \"Best of both worlds\" is the branch with every new feature; \"Port\" is the "
         "one play.bat starts. Settings a build doesn't know are simply ignored by it.");
    if (s.build == Build::Custom) {
        if (InputString("##exe", s.exe_path, "path to lbp3.exe", 520)) Refresh(a, true);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            const COMDLG_FILTERSPEC spec[] = {{L"lbp3.exe", L"lbp3.exe"}, {L"Programs", L"*.exe"}};
            auto picked = PickPaths(false, false, L"Choose lbp3.exe", spec, 2);
            if (!picked.empty()) s.exe_path = picked[0], Refresh(a, true);
        }
    }
    Dim("%s", a.p.exe.string().c_str());

    Heading("Starting");
    Toggle("Skip the logos and the intro movie", &s.skip_intro, "The PlayStation and studio logos and the live-action intro.");
    Toggle("Minimize the launcher while playing", &s.minimize_while_playing, "It comes back when the game closes.");

    Heading("Language");
    const auto& langs = Languages();
    int cur = 0;
    for (int i = 0; i < static_cast<int>(langs.size()); ++i)
        if (s.language == langs[i].code) cur = i;
    ImGui::SetNextItemWidth(S(320));
    if (ImGui::BeginCombo("##lang", langs[cur].name)) {
        for (int i = 0; i < static_cast<int>(langs.size()); ++i)
            if (ImGui::Selectable(langs[i].name, i == cur)) s.language = langs[i].code;
        ImGui::EndCombo();
    }
    Help("The US edition normally shows English or French only; any other choice here also unlocks the "
         "languages it hides (from LBPPC's finding).");

    Heading("Menus");
    Toggle("In-game menu (Insert, or L3+R3 together)", &s.menu, "Frame rate and latency, frame generation on/off, volume, a small stats corner, and Quit.");
    ImGui::BeginDisabled(!s.menu);
    Toggle("Show the stats corner from the start", &s.stats, "The in-game menu can turn it on and off too.");
    ImGui::EndDisabled();
    Toggle("\"Quit Game\" in the game's pause menu", &s.quit_menu,
           "Replaces the Good Grief! button, which is greyed out offline; press it twice. Needs Python for its "
           "setup step (LBPPC's script edit); the game files are never changed.");

    Heading("LBP1 and LBP2 items");
    Toggle("Import the LBP1/LBP2 items once (the old \"My Unlocks\")", &s.my_unlocks, "Runs the game's own import the first time you open the start menu (Options).");
    if (s.my_unlocks) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("On the first Options press the import screen appears: wait about 2 minutes, then press Cross on "
                           "\"successfully imported\". Then close the game and switch this off again. Back up your saves first "
                           "(Saves page).");
        ImGui::PopStyleColor();
    }
}

void PageGraphics(App& a) {
    Settings& s = a.s;
    Heading("Resolution");
    int r = s.res == 1080 ? 1 : 0;
    if (ImGui::RadioButton("2560x1440 (sharper; the game draws at 1440p)", &r, 0)) s.res = 1440;
    if (ImGui::RadioButton("1920x1080 (the PS4's own, stretched to the window)", &r, 1)) s.res = 1080;
    ImGui::BeginDisabled(s.res != 1440);
    Toggle("Finer glow at 1440p", &s.bloom_1440, "The glow (bloom) is drawn at the higher resolution too (from LBPPC).");
    ImGui::EndDisabled();
    Heading("Frame generation");
    Toggle("60 frames a second on screen (the game itself stays at 30)", &s.framegen, "A generated frame between each pair of real ones. About 17 ms more input lag.");
    ImGui::BeginDisabled(!s.framegen);
    Toggle("Keep in-world text readable (the map's \"Loading...\" ring)", &s.fg_text, "Generated frames keep thin bright text sharp; about 0.4 ms of GPU per generated frame.");
    ImGui::EndDisabled();
    Heading("Shaders and startup");
    const char* caches[] = {"Automatic (its own copy for builds other than the game folder's)", "This build's own copy (user\\shader_cache_best)", "The normal one (user\\shader_cache)"};
    int c = static_cast<int>(s.cache);
    ImGui::SetNextItemWidth(S(520));
    if (ImGui::Combo("Shader cache", &c, caches, IM_ARRAYSIZE(caches))) s.cache = static_cast<CacheChoice>(c), Refresh(a, true);
    Help("Where compiled shaders are kept. The best-of-both-worlds build keeps its own copy, so it never mixes with play.bat's.");
    ImGui::SetNextItemWidth(S(200));
    ImGui::SliderInt("Startup threads (0 = all cores but one)", &s.pipeline_threads, 0, 32);
    Help("How many CPU cores rebuild the cached shaders at startup (from LBPPC). 1 = one at a time, as play.bat.");
    Toggle("Save the graphics driver's own shader cache", &s.vk_pipeline_cache, "Later startups rebuild faster; the first start with a new cache file takes 6-12 s longer, once.");
    Heading("Logs");
    int flips = s.status_flips == 300 ? 1 : 0;
    if (ImGui::RadioButton("Status lines every second (as play.bat)", &flips, 0)) s.status_flips = 30;
    ImGui::SameLine();
    if (ImGui::RadioButton("Every 10 seconds (smaller logs)", &flips, 1)) s.status_flips = 300;
}

void PageControls(App& a) {
    Settings& s = a.s;
    Heading("Motion");
    Toggle("Filtered motion controls", &s.motion_filter, "The gyro's slow drift is learned away while the pad is still, and the tilt settles towards gravity. Off = the raw sensor values, as play.bat.");
    Toggle("Tilt with the number pad (4/6 and 8/2)", &s.tilt_keys, "For pads without motion sensors.");
    Heading("Touch pad");
    Toggle("The mouse as the touch pad", &s.mouse_touch, "For touch Create mode without a DualShock 4 or DualSense touch pad. F7 captures and releases the mouse; click = touch; the wheel pinches.");
    ImGui::Spacing();
    Dim("Buttons are set in user\\config\\input.ini. The Controllers page shows a pad's buttons and motion live.");
}

// The account file, read for the Online page (cached: it is read again after a change).
struct AccountView {
    bool exists = false;
    AccountFile file;
};
AccountView ReadAccountView(const Paths& p) {
    AccountView v;
    const fs::path f = AccountFilePath(p);
    std::error_code ec;
    v.exists = !f.empty() && fs::exists(f, ec);
    if (v.exists) v.file = ParseAccountFile(ReadFile(f));
    return v;
}

fs::path AccountTool(const App& a) {
    std::error_code ec;
    for (const fs::path& dir : {a.p.worktree, LauncherDir().parent_path()}) {
        const fs::path t = dir / "tools" / "create_online_account.py";
        if (!dir.empty() && fs::exists(t, ec)) return t;
    }
    return {};
}

bool IsLbppc(const std::string& server) {
    std::string n = NormalizeServer(server);
    for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return n == "https://play.lbppc.com";
}

void PageOnline(App& a) {
    Settings& s = a.s;
    const AccountView acct = ReadAccountView(a.p);
    const Settings eff = EffectiveOnline(s, a.p);
    const std::string server = NormalizeServer(eff.online_server);
    const bool name_ok = ValidOnlineName(eff.online_name);

    Heading("Online play (off unless switched on here)");
    ImGui::TextWrapped("LittleBigPlanet 3's own servers are closed; online play uses a community server. Nothing is sent anywhere unless you "
                       "switch this on and start the game, or press a button below.");
    Toggle("Online", &s.online,
           "When the game starts it signs in to the server below with your account: its levels and profiles (Refresh) and "
           "shadNet (sign-in, friends, playing together). LBPPC's server needs both. The password is read by the game from "
           "your account file; it is never put anywhere else.");
    const std::string server_hint = acct.file.server.empty() ? std::string("for example play.lbppc.com") : "from your account: " + acct.file.server;
    InputString("Server", s.online_server, server_hint.c_str(), 360);
    Help("The address of a community LittleBigPlanet server (LBPPC's is play.lbppc.com; they agreed to this port using it). "
         "Empty = the one in your account file. A server on this PC: 127.0.0.1:10061.");
    const std::string name_hint = acct.file.name.empty() ? std::string("3-16 letters, digits, - or _") : "from your account: " + acct.file.name;
    InputString("Online name", s.online_name, name_hint.c_str(), 360);
    Help("Your name online. Empty = the one in your account file (it must match the account).");
    if (ImGui::TreeNode("shadNet server")) {
        InputString("##shadnet", s.shadnet_server, ShadnetServer(eff).empty() ? "the server's own" : ShadnetServer(eff).c_str(), 360);
        Help("Only for a server whose shadNet lives elsewhere (\"host:port\" or \"tls://host:port\"). Empty = the online "
             "server's own (play.lbppc.com -> LBPPC's shadNet).");
        ImGui::TreePop();
    }
    if (!s.online_server.empty() && NormalizeServer(s.online_server).empty()) ImGui::TextColored(kBad, "That isn't a server address.");
    if (!s.online_name.empty() && !ValidOnlineName(s.online_name)) ImGui::TextColored(kBad, "The online name needs 3-16 letters, digits, - or _.");
    if (!server.empty() && name_ok)
        Dim("The game will use %s as %s (shadNet: %s).", server.c_str(), eff.online_name.c_str(), ShadnetServer(eff).c_str());
    if (s.online && (server.empty() || !name_ok)) ImGui::TextColored(kWarn, "Online stays off until there is a server and a name (or an account file).");
    if (s.online && !server.empty() && name_ok && !acct.file.ok())
        ImGui::TextColored(kWarn, "No password in the account file: shadNet won't sign in (create the account below).");

    Heading("Your account");
    if (acct.exists) {
        if (acct.file.ok()) {
            ImGui::TextColored(kGood, "%s", acct.file.name.c_str());
            ImGui::SameLine();
            Dim("on %s, password saved", acct.file.server.empty() ? "(no server noted)" : acct.file.server.c_str());
        } else {
            ImGui::TextColored(kWarn, "The account file has no name or no password.");
        }
        Dim("%s", AccountFilePath(a.p).string().c_str());
        ImGui::BeginDisabled(a.shot_mode);
        if (ImGui::SmallButton("Open the account file")) OpenPath(AccountFilePath(a.p));
        ImGui::SameLine();
        static bool show_password = false;
        if (ImGui::SmallButton(show_password ? "Hide the password" : "Show the password")) show_password = !show_password;
        ImGui::EndDisabled();
        if (show_password && !a.shot_mode) {
            ImGui::SameLine();
            ImGui::TextUnformatted(acct.file.password.c_str());
        }
        Help("The same name and password sign in to both shadNet and the server's website (Refresh).");
    } else {
        Dim("No account yet.");
        const std::string create_name = s.online_name;
        const fs::path tool = AccountTool(a);
        const bool py = PythonFound();
        const bool can = IsLbppc(eff.online_server) && ValidOnlineName(create_name) && !tool.empty() && py && !a.shot_mode && !a.account_job.running;
        ImGui::BeginDisabled(!can);
        if (ImGui::Button("Create account")) a.confirm_account = true;
        ImGui::EndDisabled();
        Help("Creates your two accounts on play.lbppc.com with the online name above and one strong random password: "
             "shadNet (sign-in, friends, playing together) and Refresh (levels and profiles). The password is saved in "
             "user\\config\\online_account.txt before anything is sent, and only its hash goes to Refresh.");
        if (!IsLbppc(eff.online_server)) Dim("Accounts can be made here for play.lbppc.com only; for another server, use its website.");
        else if (!ValidOnlineName(create_name)) Dim("Type the online name you want above first.");
        else if (!py) ImGui::TextColored(kWarn, "Making the accounts needs Python (python.org).");
        else if (tool.empty()) ImGui::TextColored(kWarn, "This build has no tools\\create_online_account.py.");
    }
    {
        std::lock_guard lock(a.account_job.m);
        if (a.account_job.running) Dim("Creating the accounts...");
    }

    Heading("Friends");
    ImGui::BeginChild("friends", ImVec2(0, S(80)), ImGuiChildFlags_Borders);
    Dim("With online on, the game's own friends list comes from shadNet. A list here (who's online, what "
                        "they're playing, join and invite) comes with the shadNet build.");
    ImGui::EndChild();

    Heading("Discord Rich Presence (off unless switched on)");
    Toggle("Show \"Playing LittleBigPlanet 3\" on Discord", &s.discord,
           "While the game runs (started from here), Discord shows the game, the time played and the level you're in. "
           "Uses Discord's local connection on this PC; nothing goes to any other server.");
    InputString("Discord application ID", s.discord_app_id, "your own application's ID (17-20 digits)", 360);
    Help("Discord shows the name and picture of the application this ID belongs to, so it must be yours: on "
         "discord.com/developers/applications press New Application, name it (for example \"LittleBigPlanet 3\"), "
         "optionally add a picture under Rich Presence > Art Assets, then copy its Application ID from the General "
         "Information page here. Never use another project's ID.");
    if (!s.discord_app_id.empty() && !ValidDiscordAppId(s.discord_app_id)) ImGui::TextColored(kBad, "An application ID is 17-20 digits.");
    ImGui::BeginDisabled(!ValidDiscordAppId(s.discord_app_id) || a.discord_test.running || a.shot_mode);
    if (ImGui::Button("Test with Discord")) {
        a.discord_test.Start();
        std::thread([&a, id = s.discord_app_id] {
            DiscordRpc rpc;
            if (!rpc.Connect(id)) return a.discord_test.Finish(false, rpc.why());
            const bool ok = rpc.SetActivity("Testing the LittleBigPlanet 3 launcher", "This goes away in 10 seconds", NowUnix());
            const std::string why = rpc.why();
            Sleep(10000);
            rpc.Clear();
            a.discord_test.Finish(ok, ok ? "Discord shows it now (for 10 seconds)" : why);
        }).detach();
    }
    ImGui::EndDisabled();
    {
        std::lock_guard lock(a.discord_test.m);
        if (a.discord_test.running) ImGui::SameLine(), Dim("talking to Discord...");
        else if (a.discord_test.done) ImGui::SameLine(), ImGui::TextColored(a.discord_test.ok ? kGood : kWarn, "%s", a.discord_test.message.c_str());
    }
}

void PageDlc(App& a) {
    if (ImGui::GetTime() - a.dlc_listed > 5.0 && !a.dlc_install.running) {
        a.dlc = ListDlc(a.p.dlc_dir);
        a.dlc_listed = ImGui::GetTime();
    }
    Heading("Installed DLC");
    Dim("%s", a.p.dlc_dir.string().c_str());
    ImGui::Text("%d packs", static_cast<int>(a.dlc.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Open the DLC folder")) OpenPath(a.p.dlc_dir);
    ImGui::SetNextItemWidth(S(300));
    ImGui::InputTextWithHint("##filter", "filter by name", a.dlc_filter, sizeof a.dlc_filter);

    Heading("Install DLC from .pkg files");
    ImGui::TextWrapped("Pick your DLC .pkg files: the launcher runs your pkg_extractor.exe on each into the DLC folder. Base game "
                       "and update packages are skipped. The game picks new packs up the next time it starts.");
    std::string tool = PkgExtractor(a).string();
    if (InputString("pkg_extractor.exe", a.s.pkg_extractor, tool.c_str(), 520)) {}
    ImGui::SameLine();
    if (ImGui::Button("Browse...##pkgx")) {
        const COMDLG_FILTERSPEC spec[] = {{L"pkg_extractor.exe", L"pkg_extractor.exe"}, {L"Programs", L"*.exe"}};
        auto picked = PickPaths(false, false, L"Choose pkg_extractor.exe", spec, 2);
        if (!picked.empty()) a.s.pkg_extractor = picked[0];
    }
    ImGui::BeginDisabled(a.dlc_install.running || a.shot_mode || a.p.dlc_dir.empty());
    if (ImGui::Button("Install DLC from .pkg...")) {
        const COMDLG_FILTERSPEC spec[] = {{L"PS4 packages", L"*.pkg"}};
        StartDlcInstall(a, PickPaths(false, true, L"Choose DLC .pkg files", spec, 1));
    }
    ImGui::EndDisabled();
    {
        std::lock_guard lock(a.dlc_install.m);
        if (a.dlc_install.running || a.dlc_install.done) {
            if (a.dlc_install.done) ImGui::TextColored(a.dlc_install.ok ? kGood : kWarn, "%s", a.dlc_install.message.c_str());
            ImGui::BeginChild("dlclog", ImVec2(0, S(110)), ImGuiChildFlags_Borders);
            for (const auto& l : a.dlc_install.lines) ImGui::TextUnformatted(l.c_str());
            if (a.dlc_install.running) ImGui::SetScrollHereY(1.f);
            ImGui::EndChild();
        }
    }

    Heading("Packs");
    std::string filter = a.dlc_filter;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ImGui::BeginTable("packs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(S(140), ImGui::GetContentRegionAvail().y - S(8))))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, S(170));
        ImGui::TableSetupColumn("For", ImGuiTableColumnFlags_WidthFixed, S(90));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& d : a.dlc) {
            if (!filter.empty()) {
                std::string n = d.title;
                std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (n.find(filter) == std::string::npos) continue;
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.title.c_str());
            ImGui::TableNextColumn();
            Dim("%s", d.label.c_str());
            ImGui::TableNextColumn();
            Dim("%s", d.title_id == "CUSA00473" ? "LBP3" : d.title_id.rfind("BCUS", 0) == 0 ? "PS3" : d.title_id.c_str());
        }
        ImGui::EndTable();
    }
}

void PageSaves(App& a) {
    Heading("Your saves");
    Dim("%s", a.p.saves.string().c_str());
    const bool running = a.game_running, marker = a.marker;
    ImGui::Text("Last backup: %s", a.backups.empty() ? "none" : (When(a.backups.front().time) + "  (" + a.backups.front().name + ")").c_str());
    if (running) ImGui::TextColored(kWarn, "Close the game first: saves are only copied while it isn't running.");
    if (marker) ImGui::TextColored(kWarn, "A test run has your saves set aside right now; wait until it finishes.");
    ImGui::BeginDisabled(running || marker || a.shot_mode);
    if (ImGui::Button("Back up saves now")) {
        std::string error;
        const std::string name = BackupSaves(a.p, "manual", &error);
        if (name.empty()) Banner(a, "Backup failed: " + error, kBad);
        else Banner(a, "Saves backed up as " + name, kGood), AppendLauncherLog(a.p, "saves backed up as " + name);
        Refresh(a, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open the saves folder")) OpenPath(a.p.saves);
    ImGui::SameLine();
    if (ImGui::Button("Open the backups folder")) OpenPath(a.p.backups);
    Heading("Restore a backup");
    ImGui::TextWrapped("Restoring first backs up the saves you have now, so nothing is lost.");
    if (ImGui::BeginTable("backups", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(S(140), ImGui::GetContentRegionAvail().y - S(8))))) {
        ImGui::TableSetupColumn("Backup", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, S(160));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(90));
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(a.backups.size()); ++i) {
            const Backup& bk = a.backups[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(bk.name.c_str());
            ImGui::TableNextColumn();
            Dim("%s", When(bk.time).c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::BeginDisabled(running || marker || a.shot_mode);
            if (ImGui::SmallButton("Restore")) a.restore_index = i;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// ---- controllers ----
void ClosePads(App& a) {
    for (auto& p : a.pads)
        if (p->pad) SDL_CloseGamepad(p->pad);
    a.pads.clear();
}

void SyncPads(App& a) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    // drop pads that went away
    a.pads.erase(std::remove_if(a.pads.begin(), a.pads.end(),
                                [&](const std::unique_ptr<PadView>& p) {
                                    for (int i = 0; i < count; ++i)
                                        if (ids[i] == p->id) return false;
                                    if (p->pad) SDL_CloseGamepad(p->pad);
                                    return true;
                                }),
                 a.pads.end());
    for (int i = 0; i < count; ++i) {
        bool have = false;
        for (auto& p : a.pads) have |= p->id == ids[i];
        if (have) continue;
        auto v = std::make_unique<PadView>();
        v->id = ids[i];
        v->pad = SDL_OpenGamepad(ids[i]);
        if (!v->pad) continue;
        if (SDL_GamepadHasSensor(v->pad, SDL_SENSOR_GYRO)) SDL_SetGamepadSensorEnabled(v->pad, SDL_SENSOR_GYRO, true);
        if (SDL_GamepadHasSensor(v->pad, SDL_SENSOR_ACCEL)) SDL_SetGamepadSensorEnabled(v->pad, SDL_SENSOR_ACCEL, true);
        a.pads.push_back(std::move(v));
    }
    SDL_free(ids);
    if (a.pad_sel >= static_cast<int>(a.pads.size())) a.pad_sel = 0;
}

void Chip(const char* label, bool on) {
    ImGui::PushStyleColor(ImGuiCol_Button, on ? kSel : ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kSel : ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? kSel : ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f));
    ImGui::Button(label, ImVec2(S(84), 0));
    ImGui::PopStyleColor(3);
}

void Stick(const char* id, float x, float y) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = S(42);
    const ImVec2 c(p.x + r, p.y + r);
    dl->AddCircleFilled(c, r, IM_COL32(30, 38, 58, 255));
    dl->AddCircle(c, r, IM_COL32(90, 120, 200, 160), 0, 1.5f);
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), IM_COL32(90, 120, 200, 60));
    dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), IM_COL32(90, 120, 200, 60));
    dl->AddCircleFilled(ImVec2(c.x + x * r, c.y + y * r), S(7), IM_COL32(255, 216, 102, 255));
    ImGui::Dummy(ImVec2(2 * r, 2 * r));
    ImGui::SameLine();
    ImGui::BeginGroup();
    Dim("%s", id);
    ImGui::Text("x %+.2f", x);
    ImGui::Text("y %+.2f", y);
    ImGui::EndGroup();
}

void PageControllers(App& a) {
    Heading("Controllers");
    if (!a.pads_allowed) {
        ImGui::TextColored(kWarn, "The game is running: it has the controllers. Close it to test a pad here.");
        return;
    }
    SyncPads(a);
    if (a.pads.empty()) {
        Dim("No controller connected. Plug one in (USB or Bluetooth) and it shows up here.");
        return;
    }
    Dim("Pad buttons don't move around this window here; hold Circle (or B) for a second, or use the "
                       "mouse or keyboard, to leave this page.");
    for (int i = 0; i < static_cast<int>(a.pads.size()); ++i) {
        if (i) ImGui::SameLine();
        const char* name = SDL_GetGamepadName(a.pads[i]->pad);
        if (ImGui::RadioButton((std::string(name ? name : "Controller") + "##" + std::to_string(i)).c_str(), a.pad_sel == i)) a.pad_sel = i;
    }
    PadView& v = *a.pads[a.pad_sel];
    SDL_Gamepad* g = v.pad;
    // motion through the port's own filter (input/motion), as the game would get it
    float gyro[3] = {0, 0, 0}, accel[3] = {0, 0, 0};
    const bool has_gyro = SDL_GamepadHasSensor(g, SDL_SENSOR_GYRO) && SDL_GetGamepadSensorData(g, SDL_SENSOR_GYRO, gyro, 3);
    const bool has_accel = SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL) && SDL_GetGamepadSensorData(g, SDL_SENSOR_ACCEL, accel, 3);
    const Uint64 now = SDL_GetTicksNS();
    const float dt = v.last_ns ? std::min(0.1f, (now - v.last_ns) / 1e9f) : 0.f;
    v.last_ns = now;
    if (has_gyro && has_accel) {
        const float kG = SDL_STANDARD_GRAVITY;  // SDL gives m/s^2; the pad data is in G
        v.filter.Sample({accel[0] / kG, accel[1] / kG, accel[2] / kG}, {gyro[0], gyro[1], gyro[2]}, dt);
    }

    ImGui::Columns(2, nullptr, false);
    Heading("Buttons");
    struct B {
        SDL_GamepadButton b;
        const char* name;
    };
    const B rows[][4] = {
        {{SDL_GAMEPAD_BUTTON_SOUTH, "Cross"}, {SDL_GAMEPAD_BUTTON_EAST, "Circle"}, {SDL_GAMEPAD_BUTTON_WEST, "Square"}, {SDL_GAMEPAD_BUTTON_NORTH, "Triangle"}},
        {{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "L1"}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "R1"}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, "L3"}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "R3"}},
        {{SDL_GAMEPAD_BUTTON_DPAD_UP, "Up"}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "Down"}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Left"}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "Right"}},
        {{SDL_GAMEPAD_BUTTON_START, "Options"}, {SDL_GAMEPAD_BUTTON_BACK, "Share"}, {SDL_GAMEPAD_BUTTON_GUIDE, "PS"}, {SDL_GAMEPAD_BUTTON_TOUCHPAD, "Touchpad"}},
    };
    for (const auto& row : rows) {
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine();
            Chip(row[i].name, SDL_GetGamepadButton(g, row[i].b));
        }
    }
    Heading("Sticks and triggers");
    auto ax = [&](SDL_GamepadAxis x) { return SDL_GetGamepadAxis(g, x) / 32767.f; };
    Stick("Left stick", ax(SDL_GAMEPAD_AXIS_LEFTX), ax(SDL_GAMEPAD_AXIS_LEFTY));
    ImGui::SameLine(0, S(30));
    Stick("Right stick", ax(SDL_GAMEPAD_AXIS_RIGHTX), ax(SDL_GAMEPAD_AXIS_RIGHTY));
    ImGui::SetNextItemWidth(S(240));
    ImGui::ProgressBar(std::max(0.f, ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)), ImVec2(S(240), 0), "L2");
    ImGui::SetNextItemWidth(S(240));
    ImGui::ProgressBar(std::max(0.f, ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)), ImVec2(S(240), 0), "R2");

    ImGui::NextColumn();
    Heading("Touch pad");
    const int pads = SDL_GetNumGamepadTouchpads(g);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 sz(S(300), S(140));
        dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(30, 38, 58, 255), S(10));
        dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(90, 120, 200, 160), S(10));
        int fingers_down = 0;
        for (int t = 0; t < pads; ++t) {
            const int nf = SDL_GetNumGamepadTouchpadFingers(g, t);
            for (int f = 0; f < nf; ++f) {
                bool down = false;
                float x = 0, y = 0, pressure = 0;
                if (SDL_GetGamepadTouchpadFinger(g, t, f, &down, &x, &y, &pressure) && down) {
                    dl->AddCircleFilled(ImVec2(p.x + x * sz.x, p.y + y * sz.y), S(9), IM_COL32(255, 216, 102, 255));
                    ++fingers_down;
                }
            }
        }
        ImGui::Dummy(sz);
        Dim(pads ? "%d finger(s) down" : "this pad has no touch pad", fingers_down);
    }
    Heading("Motion");
    if (!has_gyro || !has_accel) Dim("This pad reports no motion (or SDL can't read it).");
    else {
        Dim("Raw (SDL; X right, Y up, Z towards you)");
        ImGui::Text("Gyro   %+6.2f %+6.2f %+6.2f rad/s", gyro[0], gyro[1], gyro[2]);
        ImGui::Text("Accel  %+6.2f %+6.2f %+6.2f G", accel[0] / SDL_STANDARD_GRAVITY, accel[1] / SDL_STANDARD_GRAVITY, accel[2] / SDL_STANDARD_GRAVITY);
        // The filter as the game's pad data gets it (input/gamepad_sdl.cpp's ApplyMotion).
        const input::motion::Quat q = v.filter.Orientation();
        auto rotate = [&](float x, float y, float z) {  // q * v * q^-1
            const float tx = 2.f * (q.y * z - q.z * y), ty = 2.f * (q.z * x - q.x * z), tz = 2.f * (q.x * y - q.y * x);
            return input::motion::Vec3{x + q.w * tx + (q.y * tz - q.z * ty), y + q.w * ty + (q.z * tx - q.x * tz),
                                       z + q.w * tz + (q.x * ty - q.y * tx)};
        };
        const input::motion::Vec3 up = rotate(0.f, 1.f, 0.f), fwd = rotate(0.f, 0.f, -1.f);
        const float pitch = std::atan2(up.z, up.y) * 57.2958f;     // front up
        const float roll = std::atan2(up.x, up.y) * 57.2958f;      // right side down
        const float turn = std::atan2(-fwd.x, -fwd.z) * 57.2958f;  // turned left
        const input::motion::Vec3 acc = v.filter.Acceleration(), ang = v.filter.AngularVelocity();
        ImGui::TextColored(kAccent, "Filtered (what the game gets with the motion filter on)");
        ImGui::Text("Tilt   front up %+6.1f   right down %+6.1f   turned %+6.1f deg", pitch, roll, turn);
        ImGui::Text("Gyro   %+6.2f %+6.2f %+6.2f rad/s", ang.x, ang.y, ang.z);
        ImGui::Text("Accel  %+6.2f %+6.2f %+6.2f G", acc.x, acc.y, acc.z);
        const input::motion::Vec3 bias = v.filter.GyroBias();
        ImGui::Text("Learned gyro offset %+.4f %+.4f %+.4f rad/s", bias.x, bias.y, bias.z);
        ImGui::TextColored(v.filter.Still() ? kGood : kDim, v.filter.Still() ? "The pad is still: the offset is being learned." : "Moving.");
        if (ImGui::SmallButton("Reset orientation")) v.filter.Reset();
    }
    ImGui::Columns(1);
}

void PageNews(App& a) {
    LoadNews(a);
    std::lock_guard lock(a.news_m);
    Heading("What this build has");
    if (a.manifest.empty()) Dim("(the feature list, best/README.md, isn't next to the game folder)");
    for (const auto& [feature, sw] : a.manifest) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", feature.c_str());
        if (!sw.empty()) {
            ImGui::Indent(S(20));
            Dim("%s", sw.c_str());
            ImGui::Unindent(S(20));
        }
    }
    Heading("Recent changes");
    if (a.news.empty()) Dim("(no history: this build's folder isn't a git checkout, or git isn't installed)");
    for (const auto& c : a.news) {
        Dim("%s", c.when.c_str());
        ImGui::SameLine(S(110));
        ImGui::TextWrapped("%s", c.subject.c_str());
    }
}

void JobLog(Job& j, const char* id, float h) {
    std::lock_guard lock(j.m);
    if (!j.running && !j.done) return;
    if (j.done) ImGui::TextColored(j.ok ? kGood : kWarn, "%s", j.message.c_str());
    ImGui::BeginChild(id, ImVec2(0, S(h)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& l : j.lines) ImGui::TextUnformatted(l.c_str());
    if (j.running) ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
}

void PageTools(App& a) {
    Heading("Check the game files");
    ImGui::TextWrapped("Checks the dump: eboot.bin, param.sfo (LittleBigPlanet 3 US, 01.28), the system files, the five big "
                       "archives (their tables, and a few files inside against their hashes) and a sample of the level archives.");
    ImGui::BeginDisabled(a.file_check.running || a.shot_mode);
    if (ImGui::Button("Check game files")) StartFileCheck(a);
    ImGui::EndDisabled();
    if (a.file_check.running) {
        ImGui::SameLine();
        std::lock_guard lock(a.file_check.m);
        Dim("%s", a.file_check.lines.empty() ? "" : a.file_check.lines.back().c_str());
    }
    {
        std::lock_guard lock(a.file_results_m);
        if (!a.file_results.empty() && !a.file_check.running) {
            {
                std::lock_guard l2(a.file_check.m);
                ImGui::TextColored(a.file_check.ok ? kGood : kBad, "%s", a.file_check.message.c_str());
            }
            for (const auto& it : a.file_results) {
                ImGui::TextColored(it.level == DumpItem::Ok ? kGood : it.level == DumpItem::Warn ? kWarn : kBad,
                                   it.level == DumpItem::Ok ? "OK " : it.level == DumpItem::Warn ? "!  " : "X  ");
                ImGui::SameLine();
                ImGui::TextUnformatted(it.what.c_str());
                ImGui::SameLine();
                Dim("%s", it.detail.c_str());
            }
        }
    }

    Heading("Updates");
    ImGui::TextWrapped("For this source-built setup: checks the build's branch on the \"%s\" remote, then (only when you press it) "
                       "brings the build folder up to date and builds it. Your saves are never touched.", a.s.update_remote.c_str());
    Dim("Build folder: %s", a.p.worktree.string().c_str());
    ImGui::BeginDisabled(a.update_check.running || a.update_build.running || a.shot_mode);
    if (ImGui::Button("Check for updates")) StartUpdateCheck(a);
    ImGui::SameLine();
    const bool can_build = !a.game_running && CurrentState() != GameState::Running;
    ImGui::BeginDisabled(!can_build);
    if (ImGui::Button("Update and build")) StartUpdateBuild(a);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!can_build) ImGui::SameLine(), ImGui::TextColored(kWarn, "close the game first");
    {
        std::lock_guard lock(a.update_m);
        if (a.update.ok && !a.update.incoming.empty() && !a.update_build.running) {
            for (size_t i = 0; i < a.update.incoming.size() && i < 12; ++i) {
                ImGui::Bullet();
                ImGui::TextWrapped("%s  %s", a.update.incoming[i].when.c_str(), a.update.incoming[i].subject.c_str());
            }
            if (a.update.dirty) ImGui::TextColored(kWarn, "The build folder has local changes: updating would refuse.");
        }
    }
    JobLog(a.update_check, "updchk", 70);
    JobLog(a.update_build, "updbld", 180);

    Heading("Shortcuts");
    ImGui::BeginDisabled(a.shot_mode);
    if (ImGui::Button("Create a desktop shortcut") || false) {
        std::string error;
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        const fs::path made = CreateShortcut(ShortcutPlace::Desktop, self, &error);
        Banner(a, made.empty() ? "Shortcut failed: " + error : "Shortcut made: " + made.string(), made.empty() ? kBad : kGood);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add to the Start menu")) {
        std::string error;
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        const fs::path made = CreateShortcut(ShortcutPlace::StartMenu, self, &error);
        Banner(a, made.empty() ? "Shortcut failed: " + error : "Added to the Start menu: " + made.string(), made.empty() ? kBad : kGood);
    }
    ImGui::EndDisabled();

    Heading("Checks");
    for (const auto& c : a.checks) {
        ImGui::TextColored(c.ok ? kGood : kWarn, c.ok ? "OK " : "!  ");
        ImGui::SameLine();
        ImGui::TextUnformatted(c.what.c_str());
        ImGui::SameLine();
        Dim("%s", c.detail.c_str());
    }
    Heading("Folders and logs");
    if (ImGui::Button("Game folder")) OpenPath(a.p.root);
    ImGui::SameLine();
    if (ImGui::Button("Saves")) OpenPath(a.p.saves);
    ImGui::SameLine();
    if (ImGui::Button("DLC folder")) OpenPath(a.p.dlc_dir);
    ImGui::SameLine();
    const fs::path latest = LatestLog(a.p);
    ImGui::BeginDisabled(latest.empty());
    if (ImGui::Button("Latest log")) OpenPath(latest);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("All logs")) OpenPath(a.p.root / "analysis" / "play_logs");
    ImGui::BeginDisabled(a.shot_mode);
    if (ImGui::Button("Make a report zip")) {
        std::string error;
        const fs::path zip = MakeReport(a.s, a.p, &error);
        if (zip.empty()) Banner(a, "Report failed: " + error, kBad);
        else Banner(a, "Report saved: " + zip.string(), kGood), OpenPath(zip.parent_path());
    }
    ImGui::EndDisabled();
    Help("The latest logs, these settings and your PC's basic specs, for when something goes wrong. Your saves and online password are not included.");

    Heading("Game folder");
    ImGui::TextWrapped("The folder with user\\, analysis\\ and game_dir.txt. Leave it empty to find it automatically.");
    if (InputString("##root", a.s.game_root, "found automatically", 520)) Refresh(a, true);
    ImGui::SameLine();
    if (ImGui::Button("Browse...##root")) {
        const std::string picked = PickFolder(L"Choose the game folder");
        if (!picked.empty()) a.s.game_root = picked, Refresh(a, true);
    }
}

void PageHelp(App& a) {
    Heading("Setup");
    ImGui::BeginDisabled(a.shot_mode);
    if (ImGui::Button("Run the setup again")) a.wizard = true, a.wizard_step = 0;
    ImGui::EndDisabled();
    Help("Finds the game files and the DLC folder, checks them, and lets you choose the build, as on the first start.");

    Heading("Getting around");
    ImGui::BulletText("Mouse, or the keyboard: arrow keys and Enter; Ctrl+Tab and Ctrl+Shift+Tab change page.");
    ImGui::BulletText("Controller: the d-pad or left stick moves, Cross presses, Circle goes back; L1 and R1 change page.");
    ImGui::BulletText("While the game runs, the launcher lets go of your controllers completely.");

    Heading("In the game");
    ImGui::BulletText("Insert, or L3+R3 together: the port's own menu (frame rate, frame generation, volume, Quit).");
    ImGui::BulletText("F7: the mouse as the touch pad (when that's on, Controls page).");
    ImGui::BulletText("Your saves are in user\\savedata; the Saves page backs them up and restores them.");

    Heading("When something goes wrong");
    ImGui::BulletText("The game doesn't start: Tools > Check game files, and the Checks list there.");
    ImGui::BulletText("It closes by itself: Tools > Latest log, or Make a report zip for whoever helps you.");
    ImGui::BulletText("Shaders take a while the first time; later starts reuse them (Graphics page).");
    ImGui::BulletText("A DLC pack doesn't show up: DLC page; packs are read when the game starts.");

    Heading("About");
    Dim("LittleBigPlanet 3 Launcher 2.0, for the native port (GPL-2.0-or-later).");
    Dim("Settings: %s", SettingsFile(FindRoot(LauncherDir(), "")).string().c_str());
    Dim("Ideas from LBPPC (github.com/emurphy04/LBPPC); this launcher is our own code. The game's art "
                        "shown here comes from your own dump and is never copied anywhere.");
}

// ---- the setup wizard ----
void Wizard(App& a) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(vp->WorkSize.x - S(60), S(760)), std::min(vp->WorkSize.y - S(60), S(520))));
    ImGui::OpenPopup("Setup");
    if (!ImGui::BeginPopupModal("Setup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) return;
    const char* steps[] = {"Welcome", "Game files", "DLC", "Build", "Online", "Done"};
    for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine(), Dim(">"), ImGui::SameLine();
        if (i == a.wizard_step) ImGui::TextColored(kAccent, "%s", steps[i]);
        else Dim("%s", steps[i]);
    }
    ImGui::Separator();
    ImGui::BeginChild("wizbody", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - S(10)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
    std::error_code ec;
    switch (a.wizard_step) {
    case 0:
        ImGui::PushFont(g_bold, 24.f);
        ImGui::TextUnformatted("Welcome!");
        ImGui::PopFont();
        ImGui::TextWrapped("This launcher starts LittleBigPlanet 3 with its settings and looks after your saves, DLC and updates. "
                           "A few quick steps check that everything the game needs is in place. You can skip this and run it "
                           "again from Tools later.");
        break;
    case 1: {
        ImGui::PushFont(g_bold, 20.f);
        ImGui::TextUnformatted("Your game files");
        ImGui::PopFont();
        ImGui::TextWrapped("The game runs from your own LittleBigPlanet 3 (US, 01.28) dump: the folder that holds CUSA00473.");
        const bool found = fs::exists(a.p.game_dir / "CUSA00473" / "eboot.bin", ec);
        ImGui::TextColored(found ? kGood : kWarn, found ? "Found:" : "Not found:");
        ImGui::SameLine();
        ImGui::TextUnformatted(a.p.game_dir.empty() ? "(no game_dir.txt)" : a.p.game_dir.string().c_str());
        if (found) {
            const Sfo sfo = ReadSfo(a.p.game_dir / "CUSA00473" / "sce_sys" / "param.sfo");
            if (sfo.ok) Dim("%s, %s, version %s", sfo.Get("TITLE").c_str(), sfo.Get("TITLE_ID").c_str(), sfo.Get("APP_VER").c_str());
        }
        ImGui::BeginDisabled(a.shot_mode || a.p.root.empty());
        if (ImGui::Button("Choose the folder that holds CUSA00473...")) {
            const std::string picked = PickFolder(L"Choose the folder that holds CUSA00473");
            if (!picked.empty()) {
                if (!fs::exists(fs::path(picked) / "CUSA00473" / "eboot.bin", ec) && fs::exists(fs::path(picked) / "eboot.bin", ec))
                    Banner(a, "Choose the folder above CUSA00473 (the one that holds it).", kWarn);
                else {
                    // game_dir.txt tells lbp3.exe where the dump is; the old one is kept as .bak
                    const fs::path f = a.p.root / "game_dir.txt";
                    if (fs::exists(f, ec)) fs::copy_file(f, a.p.root / "game_dir.txt.bak", fs::copy_options::overwrite_existing, ec);
                    std::ofstream(f, std::ios::binary) << picked << "\n";
                    AppendLauncherLog(a.p, "game_dir.txt set to " + picked);
                    Refresh(a, true);
                    a.art_from.clear();
                }
            }
        }
        ImGui::EndDisabled();
        Help("Writes the folder to game_dir.txt in the game folder, which is where the game looks (the previous one is kept as game_dir.txt.bak).");
        ImGui::Spacing();
        ImGui::BeginDisabled(!found || a.file_check.running || a.shot_mode);
        if (ImGui::Button("Check the files")) StartFileCheck(a);
        ImGui::EndDisabled();
        Help("Reads the main files and a sample of the archives against their hashes (about half a minute). The full list is on the Tools page.");
        {
            std::lock_guard lock(a.file_check.m);
            if (a.file_check.running) ImGui::SameLine(), Dim("%s", a.file_check.lines.empty() ? "checking..." : a.file_check.lines.back().c_str());
            else if (a.file_check.done) ImGui::SameLine(), ImGui::TextColored(a.file_check.ok ? kGood : kBad, "%s", a.file_check.message.c_str());
        }
        if (!a.file_check.running) {
            std::lock_guard lock(a.file_results_m);
            int shown = 0;
            for (const auto& it : a.file_results)
                if (it.level == DumpItem::Bad && shown++ < 6) ImGui::TextColored(kBad, "X  %s  %s", it.what.c_str(), it.detail.c_str());
        }
        break;
    }
    case 2: {
        ImGui::PushFont(g_bold, 20.f);
        ImGui::TextUnformatted("DLC");
        ImGui::PopFont();
        const auto packs = ListDlc(a.p.dlc_dir);
        ImGui::TextWrapped("DLC you own goes in %s, one folder per pack. The game reads it every time it starts.", a.p.dlc_dir.string().c_str());
        ImGui::TextColored(packs.empty() ? kDim : kGood, "%d packs installed", static_cast<int>(packs.size()));
        ImGui::TextWrapped("To add packs, go to the DLC page after the setup: it installs them from your .pkg files.");
        break;
    }
    case 3: {
        ImGui::PushFont(g_bold, 20.f);
        ImGui::TextUnformatted("Which build to play");
        ImGui::PopFont();
        const char* builds[] = {"This launcher's build", "Best of both worlds", "Port (as play.bat)", "Another lbp3.exe"};
        int b = static_cast<int>(a.s.build);
        for (int i = 0; i < 3; ++i)
            if (ImGui::RadioButton(builds[i], &b, i)) a.s.build = static_cast<Build>(i), Refresh(a, true);
        const bool ok = fs::exists(a.p.exe, ec);
        ImGui::TextColored(ok ? kGood : kWarn, ok ? "Found:" : "Not found:");
        ImGui::SameLine();
        ImGui::TextUnformatted(a.p.exe.string().c_str());
        break;
    }
    case 4: {
        ImGui::PushFont(g_bold, 20.f);
        ImGui::TextUnformatted("Online");
        ImGui::PopFont();
        const AccountView acct = ReadAccountView(a.p);
        if (acct.file.ok()) {
            ImGui::TextWrapped("You have an online account: %s%s%s.", acct.file.name.c_str(), acct.file.server.empty() ? "" : " on ",
                               acct.file.server.c_str());
            ImGui::Checkbox("Play online with it", &a.s.online);
            Help("Signs in to the server (levels, profiles) and shadNet (friends, playing together) when the game starts. "
                 "You can switch it off any time on the Online page.");
        } else {
            ImGui::TextWrapped("Online play uses a community server and an account there. It stays off until you set it up "
                               "on the Online page, where you can also create the account.");
        }
        break;
    }
    default:
        ImGui::PushFont(g_bold, 24.f);
        ImGui::TextUnformatted("All set");
        ImGui::PopFont();
        ImGui::TextWrapped("Press Play to start the game. Settings are on the pages on the left; Tools has the file check, "
                           "updates, shortcuts and this setup again.");
        if (!EssentialsOk(a)) ImGui::TextColored(kWarn, "Something is still missing; the Tools page lists it.");
        break;
    }
    ImGui::EndChild();
    if (ImGui::Button("Skip")) a.wizard = false, a.s.setup_done = true, ImGui::CloseCurrentPopup();
    ImGui::SameLine(ImGui::GetWindowWidth() - S(250));
    ImGui::BeginDisabled(a.wizard_step == 0);
    if (ImGui::Button("Back", ImVec2(S(110), 0))) --a.wizard_step;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (a.wizard_step < 5) {
        if (ImGui::Button("Next", ImVec2(S(110), 0))) ++a.wizard_step;
    } else if (ImGui::Button("Finish", ImVec2(S(110), 0))) {
        a.wizard = false;
        a.s.setup_done = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---- header ----
void Header(App& a) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = S(150);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + vp->WorkSize.x, p0.y + h);
    if (a.art.tex) {
        // pic1 cropped to the banner, darkened towards the left for the text
        const float ar = (p1.x - p0.x) / h, tar = static_cast<float>(a.art.w) / a.art.h;
        float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
        if (tar > ar) u0 = (1 - ar / tar) / 2, u1 = 1 - u0;
        else v0 = (1 - tar / ar) * 0.35f, v1 = v0 + tar / ar;
        dl->AddImage(Ref(a.art), p0, p1, ImVec2(u0, v0), ImVec2(u1, v1));
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(10, 12, 22, 235), IM_COL32(10, 12, 22, 120), IM_COL32(10, 12, 22, 120), IM_COL32(10, 12, 22, 235));
    } else {
        dl->AddRectFilledMultiColor(p0, p1, IM_COL32(26, 40, 84, 255), IM_COL32(18, 22, 34, 255), IM_COL32(18, 22, 34, 255), IM_COL32(26, 40, 84, 255));
    }
    dl->AddLine(ImVec2(p0.x, p1.y), p1, IM_COL32(90, 120, 200, 160), 1.5f);
    float x = p0.x + S(22);
    if (a.icon.tex) {
        const float sz = h - S(44);
        dl->AddImageRounded(Ref(a.icon), ImVec2(x, p0.y + S(22)), ImVec2(x + sz, p0.y + S(22) + sz), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(14));
        x += sz + S(20);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y + S(26)));
    ImGui::PushFont(g_bold, 34.f);
    ImGui::TextUnformatted("LittleBigPlanet 3");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y + S(72)));
    const char* build = a.s.build == Build::Best ? "best of both worlds" : a.s.build == Build::Port ? "port" : a.s.build == Build::Custom ? "custom build" : "this launcher's build";
    Dim("native port  ·  %s", build);

    // state
    GameState state;
    std::string note, error, level, discord;
    long code;
    {
        std::lock_guard lock(g_run.m);
        state = g_run.state;
        note = g_run.note;
        error = g_run.error;
        code = g_run.exit_code;
        level = g_run.level;
        discord = g_run.discord;
    }
    const bool running_other = state != GameState::Running && state != GameState::Starting && a.game_running;
    const bool ok = EssentialsOk(a);
    const bool updating = a.update_build.running;
    const bool can_play = ok && !a.marker && !running_other && !updating && (state == GameState::Idle || state == GameState::Ended) && !a.shot_mode;

    // Play
    const float bw = S(210), bh = S(62);
    ImGui::SetCursorScreenPos(ImVec2(p1.x - bw - S(26), p0.y + S(26)));
    ImGui::PushStyleColor(ImGuiCol_Button, can_play ? kPlay : ImVec4(0.25f, 0.27f, 0.32f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kPlayHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kPlayHover);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(12));
    ImGui::PushFont(g_bold, 28.f);
    ImGui::BeginDisabled(!can_play);
    const char* label = state == GameState::Starting ? "Starting..." : state == GameState::Running ? "Playing" : "PLAY";
    if (ImGui::Button(label, ImVec2(bw, bh))) {
        SaveSettings(a.s);
        a.saved_ini = ToIni(a.s);
        Refresh(a, true);
        {
            std::lock_guard lock(g_run.m);
            g_run.state = GameState::Starting;
            g_run.note.clear();
            g_run.error.clear();
            g_run.discord.clear();
        }
        std::thread(LaunchWorker, a.s, a.p).detach();
        Banner(a, "Starting the game...", kDim);
    }
    ImGui::SetItemDefaultFocus();  // the controller's cursor starts on Play
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // status line
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y + S(104)));
    if (a.marker) ImGui::TextColored(kWarn, "A test run has your saves set aside; Play waits until it finishes.");
    else if (updating) ImGui::TextColored(kWarn, "Updating and building (Tools); Play waits until it finishes.");
    else if (running_other) ImGui::TextColored(kWarn, "LittleBigPlanet 3 is already running.");
    else if (!ok) ImGui::TextColored(kWarn, "Something is missing: the setup (Tools) helps.");
    else if (state == GameState::Running) {
        ImGui::TextColored(kGood, "Playing%s%s", level.empty() ? "" : ": ", level.c_str());
        if (!discord.empty()) ImGui::SameLine(), Dim("  %s", discord.c_str());
    } else if (state == GameState::Ended && code != 0) {
        ImGui::TextColored(kBad, "%s", error.empty() ? ("The game stopped with an error (code " + std::to_string(code) + ").").c_str() : error.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open the log")) OpenPath(LatestLog(a.p));
    } else if (!a.banner.empty()) ImGui::TextColored(a.banner_color, "%s", a.banner.c_str());
    else {
        const Settings eff = EffectiveOnline(a.s, a.p);
        const bool on = eff.online && !NormalizeServer(eff.online_server).empty() && ValidOnlineName(eff.online_name);
        Dim("Ready.  %s", on ? ("Online on " + NormalizeServer(eff.online_server) + " as " + eff.online_name + ".").c_str() : "Offline.");
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    ImGui::Dummy(ImVec2(0, 0));
}

void Sidebar(App& a, float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kSide);
    ImGui::BeginChild("side", ImVec2(width, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2(0, S(8)));
    for (int i = 0; i < kPages; ++i) {
        const bool sel = a.page == i;
        ImGui::PushStyleColor(ImGuiCol_Header, kSel);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.f, 0.5f));
        if (sel) ImGui::PushFont(g_bold, 18.f);
        if (ImGui::Selectable((std::string("   ") + kPageNames[i]).c_str(), sel, 0, ImVec2(0, S(38)))) a.page = static_cast<Page>(i);
        if (sel) ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - S(54));
    Dim("  L1 / R1 or Ctrl+Tab");
    Dim("  change page");
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void Modals(App& a) {
    if (a.restore_index >= 0) ImGui::OpenPopup("Restore saves?");
    if (ImGui::BeginPopupModal("Restore saves?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (a.restore_index < 0 || a.restore_index >= static_cast<int>(a.backups.size())) {
            a.restore_index = -1;
            ImGui::CloseCurrentPopup();
        } else {
            const Backup& bk = a.backups[a.restore_index];
            ImGui::Text("Replace your saves with \"%s\" (%s)?", bk.name.c_str(), When(bk.time).c_str());
            Dim("Your current saves are backed up first.");
            if (ImGui::Button("Restore", ImVec2(S(140), 0))) {
                std::string error, safety;
                if (GameRunning()) Banner(a, "Close the game first.", kWarn);
                else if (RestoreBackup(a.p, bk, &error, &safety)) {
                    Banner(a, "Restored " + bk.name + (safety.empty() ? "" : " (your previous saves: " + safety + ")"), kGood);
                    AppendLauncherLog(a.p, "restored " + bk.name + ", previous saves kept as " + safety);
                } else Banner(a, "Restore failed: " + error, kBad);
                a.restore_index = -1;
                Refresh(a, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(S(140), 0))) a.restore_index = -1, ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (a.confirm_account) ImGui::OpenPopup("Create accounts?"), a.confirm_account = false;
    if (ImGui::BeginPopupModal("Create accounts?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Create the accounts \"%s\" on play.lbppc.com?", a.s.online_name.c_str());
        Dim("Your online name goes to LBPPC's shadNet and Refresh; the password is made here, saved first,");
        Dim("sent to shadNet over an encrypted connection, and only as a hash to Refresh.");
        if (ImGui::Button("Create", ImVec2(S(140), 0))) {
            a.account_job.Start();
            const fs::path tool = AccountTool(a), python = PythonPath();
            std::thread([&a, tool, python, name = a.s.online_name, p = a.p] {
                AppendLauncherLog(p, "creating the online accounts on play.lbppc.com");
                const ProcResult r = RunCapture(Quote(python.wstring()) + L" " + Quote(tool.wstring()) + L" " + Quote(fs::path(name).wstring()) +
                                                    L" --server play.lbppc.com --root " + Quote(p.root.wstring()),
                                                p.root, [&](const std::string& l) { if (!l.empty()) a.account_job.Line(l); }, nullptr, 120000);
                const bool ok = r.started && r.exit_code == 0;
                AppendLauncherLog(p, ok ? "online accounts created (the password is in user\\config\\online_account.txt)"
                                        : "online accounts: not created (" + (r.error.empty() ? "exit " + std::to_string(r.exit_code) : r.error) + ")");
                a.account_job.Finish(ok, ok ? "Both accounts are ready." : "The accounts weren't (all) created; see below.");
            }).detach();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(S(140), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    static bool account_show = false;
    {
        std::lock_guard lock(a.account_job.m);
        if (a.account_job.done) a.account_job.done = false, account_show = true, a.account_result = a.account_job.lines, a.account_ok = a.account_job.ok, a.account_message = a.account_job.message;
    }
    if (account_show) ImGui::OpenPopup("Accounts"), account_show = false;
    if (ImGui::BeginPopupModal("Accounts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(a.account_ok ? kGood : kBad, "%s", a.account_message.c_str());
        for (const auto& l : a.account_result) ImGui::TextUnformatted(l.c_str());
        if (a.account_ok) Dim("The password is in user\\config\\online_account.txt (the Online page can show it).");
        if (ImGui::Button("OK", ImVec2(S(140), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Frame(App& a) {
    Refresh(a);
    LoadArt(a);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    Header(a);
    const float side = S(190);
    Sidebar(a, side);
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22), S(12)));
    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    switch (a.page) {
    case Home: PageHome(a); break;
    case GamePage: PageGame(a); break;
    case Graphics: PageGraphics(a); break;
    case Controls: PageControls(a); break;
    case Online: PageOnline(a); break;
    case Dlc: PageDlc(a); break;
    case Saves: PageSaves(a); break;
    case Controllers: PageControllers(a); break;
    case News: PageNews(a); break;
    case Tools: PageTools(a); break;
    default: PageHelp(a); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (a.wizard) Wizard(a);
    Modals(a);
    ImGui::End();
    if (!a.shot_mode) {
        const std::string ini = ToIni(a.s);
        if (ini != a.saved_ini && SaveSettings(a.s)) a.saved_ini = ini;
    }
}

bool SaveShot(SDL_Renderer* r, const std::string& path) {
    SDL_Surface* surf = SDL_RenderReadPixels(r, nullptr);
    if (!surf) return false;
    const bool ok = SDL_SaveBMP(surf, path.c_str());
    SDL_DestroySurface(surf);
    return ok;
}

void SetWindowIconFromResource(SDL_Window* w) {
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HICON big = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    HICON little = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (little) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(little));
}

// The saved window rectangle, if it still lies on a display.
bool RectOnDisplay(int x, int y, int w, int h) {
    int count = 0;
    SDL_DisplayID* ds = SDL_GetDisplays(&count);
    bool on = false;
    for (int i = 0; ds && i < count && !on; ++i) {
        SDL_Rect r;
        if (SDL_GetDisplayUsableBounds(ds[i], &r))
            on = x + w / 2 >= r.x && x + w / 2 < r.x + r.w && y + S(20) >= r.y && y + S(20) < r.y + r.h;
    }
    SDL_free(ds);
    return on;
}

}  // namespace

int main(int argc, char** argv) {
    std::string shot_dir, print_file;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0) shot_dir = argv[i + 1];
        if (std::strcmp(argv[i], "--print-launch") == 0) print_file = argv[i + 1];
    }
    if (!print_file.empty()) {
        const Settings s = LoadSettings();
        const Paths p = ResolvePaths(s);
        std::ofstream o(print_file);
        o << "exe=" << p.exe.string() << "\ncwd=" << p.root.string() << "\nlog=analysis\\" << p.log_base << "_log.txt\nargs=";
        for (const auto& arg : MapSettings(s).args) o << arg << " ";
        o << "\n";
        for (const auto& [k, v] : GameEnvironment(s, p, {})) o << k << "=" << v << "\n";
        if (s.quit_menu)
            o << "# Quit Game: python " << (p.worktree / "tools" / "quit_menu_setup.py").string() << " would run first, then LBP3_FILE_EDITS="
              << (p.worktree / "user" / "file_edits.bin").string() << " and LBP3_QUIT_MENU=1 are added\n";
        for (const auto& c : RunChecks(s, p)) o << (c.ok ? "# ok   " : "# FAIL ") << c.what << ": " << c.detail << "\n";
        return 0;
    }
    const bool shot = !shot_dir.empty();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetTaskbarIdentity();
    // Controllers only for the real window, and only while the game isn't running: the game owns
    // them then (the screenshot run never touches them at all).
    const bool pads_at_start = !shot && !GameRunning();
    if (!SDL_Init(SDL_INIT_VIDEO | (pads_at_start ? SDL_INIT_GAMEPAD : 0))) {
        MessageBoxA(nullptr, SDL_GetError(), "LBP3 Launcher", MB_ICONERROR);
        return 1;
    }
    App a;
    a.s = LoadSettings();
    a.saved_ini = ToIni(a.s);
    a.shot_mode = shot;
    a.pads_allowed = shot || pads_at_start;
    g_scale = [] {
        const float s = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        return s > 0.f ? s : 1.f;
    }();
    int w = a.s.win_w > 0 ? a.s.win_w : static_cast<int>(1120 * g_scale);
    int h = a.s.win_h > 0 ? a.s.win_h : static_cast<int>(760 * g_scale);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    if (shot) {
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        flags |= SDL_WINDOW_NOT_FOCUSABLE;
        w = static_cast<int>(1120 * g_scale);
        h = static_cast<int>(760 * g_scale);
    }
    a.window = SDL_CreateWindow("LittleBigPlanet 3 Launcher", w, h, flags);
    a.renderer = a.window ? SDL_CreateRenderer(a.window, nullptr) : nullptr;
    if (!a.renderer) {
        MessageBoxA(nullptr, SDL_GetError(), "LBP3 Launcher", MB_ICONERROR);
        return 1;
    }
    SetWindowIconFromResource(a.window);
    SDL_SetRenderVSync(a.renderer, 1);
    SDL_SetWindowMinimumSize(a.window, static_cast<int>(820 * g_scale), static_cast<int>(600 * g_scale));
    if (shot) {
        // a secondary display if there is one, so the player's main screen is never covered
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        for (int i = 0; displays && i < count; ++i) {
            if (displays[i] == primary) continue;
            SDL_Rect r;
            if (SDL_GetDisplayBounds(displays[i], &r)) SDL_SetWindowPosition(a.window, r.x + 40, r.y + 40);
            break;
        }
        SDL_free(displays);
    } else if (a.s.win_x != -1 && a.s.win_y != -1 && RectOnDisplay(a.s.win_x, a.s.win_y, w, h)) {
        SDL_SetWindowPosition(a.window, a.s.win_x, a.s.win_y);
    } else {
        SDL_SetWindowPosition(a.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_ShowWindow(a.window);
    if (!shot && a.s.win_max) SDL_MaximizeWindow(a.window);
    // the scale of the display the window ended up on
    if (const float ws = SDL_GetWindowDisplayScale(a.window); ws > 0.f) g_scale = ws;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | (shot ? 0 : ImGuiConfigFlags_NavEnableGamepad);
    const char* windir = std::getenv("WINDIR");
    const std::string fonts = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    std::error_code ec;
    if (fs::exists(fonts + "segoeui.ttf", ec)) g_font = io.Fonts->AddFontFromFileTTF((fonts + "segoeui.ttf").c_str(), 0.f);
    if (fs::exists(fonts + "seguisb.ttf", ec)) g_bold = io.Fonts->AddFontFromFileTTF((fonts + "seguisb.ttf").c_str(), 0.f);
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    if (!g_bold) g_bold = g_font;
    io.FontDefault = g_font;
    ApplyStyle(g_scale);
    ImGui_ImplSDL3_InitForSDLRenderer(a.window, a.renderer);
    ImGui_ImplSDLRenderer3_Init(a.renderer);
    if (!pads_at_start) ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);

    Refresh(a, true);
    a.dlc = ListDlc(a.p.dlc_dir);
    a.dlc_listed = 0;
    StartStats(a);
    LoadNews(a);
    if (!a.s.setup_done && !shot) a.wizard = true;

    // --shot: every page, then the wizard
    int shot_index = 0, shot_frames = 0;
    if (shot) a.page = Home;

    bool running = true, minimized_for_game = false;
    GameState last_state = GameState::Idle;
    Uint64 last_game_poll = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(a.window)) running = false;
            if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                const float ns = SDL_GetWindowDisplayScale(a.window);
                if (ns > 0.f && std::fabs(ns - g_scale) > 0.01f) g_scale = ns, ApplyStyle(g_scale);
            }
            if (!shot && (e.type == SDL_EVENT_WINDOW_MOVED || e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_MAXIMIZED || e.type == SDL_EVENT_WINDOW_RESTORED)) {
                const SDL_WindowFlags f = SDL_GetWindowFlags(a.window);
                if (!(f & SDL_WINDOW_MINIMIZED)) {
                    a.s.win_max = (f & SDL_WINDOW_MAXIMIZED) != 0;
                    if (!a.s.win_max) {
                        SDL_GetWindowPosition(a.window, &a.s.win_x, &a.s.win_y);
                        SDL_GetWindowSize(a.window, &a.s.win_w, &a.s.win_h);
                    }
                }
            }
            // L1 / R1 (and Ctrl+Tab) change the page; on the controller tester only the hold-Circle exit works
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && a.page != Controllers && !a.wizard) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) a.page = static_cast<Page>((a.page + kPages - 1) % kPages);
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) a.page = static_cast<Page>((a.page + 1) % kPages);
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB && (e.key.mod & SDL_KMOD_CTRL) && !a.wizard)
                a.page = static_cast<Page>((a.page + ((e.key.mod & SDL_KMOD_SHIFT) ? kPages - 1 : 1)) % kPages);
        }
        // The game, started from here or not: while it runs the launcher lets go of the
        // controllers completely (SDL's gamepad side shut down, so it never talks to the pad),
        // and takes them back when it ends. Checked even while minimised.
        const GameState state = CurrentState();
        if (!shot && SDL_GetTicks() - last_game_poll > 2000) {
            a.game_running = GameRunning();
            last_game_poll = SDL_GetTicks();
        }
        const bool playing = state == GameState::Starting || state == GameState::Running || a.game_running;
        if (!shot && playing == a.pads_allowed) {
            a.pads_allowed = !playing;
            if (playing) {
                ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
                ClosePads(a);
                SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            } else if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
                ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_AutoFirst, nullptr, 0);
            }
        }
        if (state != last_state) {
            if (state == GameState::Ended) StartStats(a);
            last_state = state;
        }
        if (state == GameState::Running && a.s.minimize_while_playing && !minimized_for_game) {
            SDL_MinimizeWindow(a.window);
            minimized_for_game = true;
        } else if (state != GameState::Running && state != GameState::Starting && minimized_for_game) {
            SDL_RestoreWindow(a.window);
            SDL_RaiseWindow(a.window);
            minimized_for_game = false;
        }
        if (SDL_GetWindowFlags(a.window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(100);
            continue;
        }
        // the controller tester owns the pad's buttons: no UI navigation with it there
        if (a.page == Controllers) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
            bool circle = false;
            for (auto& p : a.pads)
                if (p->pad && SDL_GetGamepadButton(p->pad, SDL_GAMEPAD_BUTTON_EAST)) circle = true;
            const double t = SDL_GetTicks() / 1000.0;
            if (!circle) a.circle_held = 0;
            else if (a.circle_held == 0) a.circle_held = t;
            else if (t - a.circle_held > 1.0) a.page = Home, a.circle_held = 0;
        } else if (!shot) {
            if (a.pads_allowed) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
            if (!a.pads.empty()) ClosePads(a);
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        Frame(a);
        ImGui::Render();
        SDL_SetRenderDrawColor(a.renderer, 18, 22, 34, 255);
        SDL_RenderClear(a.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), a.renderer);
        // (the first picture waits for the play stats and the history, read in the background)
        const bool shot_ready = a.news_ready && !a.stats_job.running;
        if (shot && shot_ready && ++shot_frames == 12) {
            // pages first, then the wizard's steps
            const std::string name = shot_index < kPages ? std::string(kPageKeys[shot_index]) : "setup_" + std::to_string(shot_index - kPages);
            SaveShot(a.renderer, shot_dir + "\\launcher_" + name + ".bmp");
            shot_frames = 0;
            ++shot_index;
            if (shot_index < kPages) a.page = static_cast<Page>(shot_index);
            else if (shot_index < kPages + 6) a.page = Home, a.wizard = true, a.wizard_step = shot_index - kPages;
            else running = false;
        }
        SDL_RenderPresent(a.renderer);
    }
    {
        // a game started from here keeps running; only the launcher closes
        std::lock_guard lock(g_run.m);
        if (g_run.state == GameState::Running) g_run.state = GameState::Ended;  // stops its watcher thread
    }
    if (!shot) SaveSettings(a.s);
    ClosePads(a);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    for (Texture* t : {&a.art, &a.icon})
        if (t->tex) SDL_DestroyTexture(t->tex);
    SDL_DestroyRenderer(a.renderer);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    CoUninitialize();
    return 0;
}
