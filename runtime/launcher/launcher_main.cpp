// LBP3Launcher: the window for starting the native LittleBigPlanet 3 with its settings, and for
// everything around it: the first-run setup and a short tour, the game file check, DLC, saves,
// updates (releases for a packaged install, git for a dev checkout), the online hub (server status,
// sign-in test, accounts, news), friends, the community's levels, Discord, a controller tester and
// a controls editor, play stats and shortcuts. SDL3 + Dear ImGui in a warm cardboard-and-stitches
// look, with the game's own art read from the player's dump at run time. Our own code; LBPPC's
// launcher was only an idea (its code is GPL-3.0 and not reused here).
//
//   LBP3Launcher.exe                       the window
//   LBP3Launcher.exe --shot DIR            draws every page (and the setup and tour) into
//     [--shot-server http://127.0.0.1:N]   DIR\launcher_<page>.bmp on a secondary display without
//                                          taking the focus, with a fixed animation clock, and
//                                          exits. Nothing is started or sent, except to the
//                                          --shot-server mock (127.0.0.1 only) if given.
//   LBP3Launcher.exe --print-launch FILE   what Play would start with the saved settings
#include "launcher_app.h"

#include <shobjidl.h>

#include <SDL3/SDL_main.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

#include "launcher_discord.h"
#include "launcher_proc.h"
#include "launcher_shell.h"
#include "launcher_theme.h"
#include "input/action_table.h"

namespace launcher {
extern std::string g_shadnetLogPath;  // launcher_net_glue.cpp
}

namespace launcher::ui {

Run g_run;
const char* const kPageNames[kPages] = {"Home", "Community", "Online", "Friends", "Game", "Graphics & sound", "Controls",
                                        "DLC", "Saves", "Controllers", "What's new", "Tools", "Help"};
const char* const kPageKeys[kPages] = {"home", "community", "online", "friends", "game", "graphics", "controls",
                                       "dlc", "saves", "controllers", "news", "tools", "help"};
static const Icon kPageIcons[kPages] = {Icon::Home, Icon::Community, Icon::Online, Icon::Friends, Icon::Game, Icon::Graphics, Icon::Controls,
                                        Icon::Dlc, Icon::Saves, Icon::Pad, Icon::News, Icon::Tools, Icon::Help};

void Job::Start() {
    std::lock_guard lock(m);
    running = true;
    cancel = false;
    done = ok = false;
    progress = -1.f;
    message.clear();
    lines.clear();
}
void Job::Line(const std::string& s) {
    std::lock_guard lock(m);
    lines.push_back(s);
    if (lines.size() > 4000) lines.erase(lines.begin(), lines.begin() + 1000);
}
void Job::Finish(bool success, const std::string& msg) {
    std::lock_guard lock(m);
    ok = success;
    message = msg;
    done = true;
    running = false;
}

GameState CurrentState() {
    std::lock_guard lock(g_run.m);
    return g_run.state;
}

void ToastMsg(App& a, const std::string& text, ImVec4 color) {
    a.toasts.push_back({text, color, a.time});
    if (a.toasts.size() > 4) a.toasts.erase(a.toasts.begin());
}

void FreeTexture(Texture& t) {
    if (t.tex) SDL_DestroyTexture(t.tex);
    t = Texture{};
}

static AccountView ReadAccountView(const Paths& p) {
    AccountView v;
    const fs::path f = AccountFilePath(p);
    std::error_code ec;
    v.exists = !f.empty() && fs::exists(f, ec);
    if (v.exists) v.file = ParseAccountFile(ReadFile(f));
    return v;
}

void Refresh(App& a, bool now) {
    const double t = ImGui::GetTime();
    if (!now && t - a.last_refresh < 1.5) return;
    a.last_refresh = t;
    a.p = ResolvePaths(a.s);
    a.checks = RunChecks(a.s, a.p);
    a.backups = ListBackups(a.p);
    // The pictures (--shot) show the launcher as when nothing else is going on.
    a.game_running = !a.shot_mode && GameRunning();
    a.marker = !a.shot_mode && TestMarkerSet(a.p);
    a.acct = ReadAccountView(a.p);
}

Settings Eff(const App& a) {
    Settings e = a.s;
    if (!a.acct.file.name.empty()) e.online_name = a.acct.file.name;
    if (!a.acct.file.server.empty()) e.online_server = a.acct.file.server;
    return e;
}

std::string OnlineServer(const App& a) {
    if (a.shot_mode) return a.shot_server;  // only the local mock, if any
    return NormalizeServer(Eff(a).online_server);
}

bool EssentialsOk(const App& a) {
    for (const auto& c : a.checks)
        if (!c.ok && (c.what == "lbp3.exe" || c.what == "Game files (the dump)" || c.what == "Game folder (saves, logs)")) return false;
    return true;
}

bool GameBusy(const App& a) {
    const GameState st = CurrentState();
    return a.game_running || st == GameState::Starting || st == GameState::Running;
}

void StopFriends(App& a, bool wait) {
    FriendsState& f = a.friends;
    if (f.stopper.joinable() && !f.stopping) f.stopper.join();
    if (!f.session.Active() && !f.stopping) return;
    if (wait) {
        if (f.stopper.joinable()) f.stopper.join();
        f.session.Stop();
        return;
    }
    if (f.stopping) return;
    f.stopping = true;
    f.stopper = std::thread([&f] {
        f.session.Stop();
        f.stopping = false;
    });
}

std::string Narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

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

static void LaunchWorker(Settings s, Paths p) {
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
                rpc.SetActivity("Playing LittleBigPlanet 3", current, started);
                shown_level = current;  // once per change, even if Discord said no (not every 2 s)
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
    FreeTexture(a.art);
    FreeTexture(a.icon);
    std::error_code ec;
    fs::path pic = sys / "pic1.png";
    if (!fs::exists(pic, ec)) pic = sys / "pic0.png";
    a.art = MakeTexture(a.renderer, Downscale(LoadImageFile(pic), 1600, 900));
    a.icon = MakeTexture(a.renderer, Downscale(LoadImageFile(sys / "icon0.png"), 256, 256));
}

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

static std::vector<std::pair<std::string, std::string>> ReadManifest(const fs::path& root) {
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
    // A package has no git history (and the folder above it may be some other checkout): it shows
    // its RELEASE-NOTES.md instead.
    std::thread([&a, wt = a.p.worktree, root = a.p.root, portable = a.portable] {
        auto changes = portable ? std::vector<Change>{} : RecentChanges(wt, 40);
        auto manifest = portable ? std::vector<std::pair<std::string, std::string>>{} : ReadManifest(root);
        auto highlights = ReadReleaseHighlights(LauncherDir());
        std::lock_guard lock(a.news_m);
        a.news = std::move(changes);
        a.manifest = std::move(manifest);
        a.highlights = std::move(highlights);
        a.news_ready = true;
    }).detach();
}

static bool SaveShot(SDL_Renderer* r, const std::string& path) {
    SDL_Surface* surf = SDL_RenderReadPixels(r, nullptr);
    if (!surf) return false;
    const bool ok = SDL_SaveBMP(surf, path.c_str());
    SDL_DestroySurface(surf);
    return ok;
}

static void SetWindowIconFromResource(SDL_Window* w) {
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HICON big = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    HICON little = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (little) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(little));
}

static bool RectOnDisplay(int x, int y, int w, int h) {
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

void PlayGame(App& a) {
    // The game signs in to shadNet itself, and an account can't be signed in twice: the Friends
    // page's session ends first (it takes a moment at most).
    StopFriends(a, true);
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
    ToastMsg(a, "Starting LittleBigPlanet 3...", kInfo);
}

namespace {

ImTextureRef Ref(const Texture& t) { return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t.tex))); }

void Mark(App& a, const char* name) { a.marks[name] = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()}; }

// The status pill's words and colour.
void StatusPill(App& a, GameState state, const std::string& level) {
    const FriendsSession::View fv = a.friends.session.Snapshot();
    const Settings eff = Eff(a);
    const bool onlineReady = eff.online && !NormalizeServer(eff.online_server).empty() && ValidOnlineName(eff.online_name);
    std::string text;
    ImVec4 dot = kDim;
    bool pulse = false;
    if (state == GameState::Starting) text = "Starting...", dot = kWarn, pulse = true;
    else if (state == GameState::Running) text = level.empty() ? "Playing" : "Playing: " + level, dot = kGood, pulse = true;
    else if (a.game_running) text = "LittleBigPlanet 3 is running", dot = kInfo;
    else if (fv.status == FriendsSession::Status::SigningIn) text = "Signing in...", dot = kWarn, pulse = true;
    else if (fv.status == FriendsSession::Status::SignedIn) text = "Signed in as " + fv.name, dot = kGood;
    else if (onlineReady) text = "Online as " + eff.online_name, dot = kGood;
    else if (eff.online) text = "Online: an account is needed", dot = kWarn;
    else text = "Offline", dot = kDim;
    Pill(text.c_str(), dot, pulse);
    Mark(a, "pill");
}

void Header(App& a) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = S(176);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + vp->WorkSize.x, p0.y + h);
    // the header lets more of the art through; a soft edge below it
    dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - S(40)), p1, Col(ImVec4(0, 0, 0, 0)), Col(ImVec4(0, 0, 0, 0)), Col(kBg, 0.55f), Col(kBg, 0.55f));
    Stitches(dl, ImVec2(p0.x, p1.y - S(1)), ImVec2(p1.x, p1.y - S(1)), Col(kStitch, 0.35f), S(8), S(6), S(1.5f));
    float x = p0.x + S(26);
    if (a.icon.tex) {
        const float sz = S(118);
        const ImVec2 i0(x, p0.y + S(28));
        const float bob = a.shot_mode ? 0.f : S(2) * std::sin(static_cast<float>(a.time) * 1.3f);
        dl->AddRectFilled(ImVec2(i0.x + S(3), i0.y + S(6) + bob), ImVec2(i0.x + sz + S(3), i0.y + sz + S(6) + bob), Col(ImVec4(0, 0, 0, 0.45f)), S(20));
        dl->AddImageRounded(Ref(a.icon), ImVec2(i0.x, i0.y + bob), ImVec2(i0.x + sz, i0.y + sz + bob), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(20));
        StitchedRect(dl, ImVec2(i0.x, i0.y + bob), ImVec2(i0.x + sz, i0.y + sz + bob), Col(kStitch, 0.7f), S(6), S(20));
        x += sz + S(24);
    }
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y + S(26)));
    ImGui::PushFont(g_black, 40.f);
    const ImVec2 tp = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(tp.x + S(2), tp.y + S(3)), Col(ImVec4(0, 0, 0, 0.5f)), "LittleBigPlanet 3");
    ImGui::TextUnformatted("LittleBigPlanet 3");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(x + S(2), p0.y + S(80)));
    const char* build = a.portable ? "packaged" : a.s.build == Build::Best ? "best of both worlds" : a.s.build == Build::Port ? "port" : a.s.build == Build::Custom ? "custom build" : "this launcher's build";
    const std::string& version = a.version;
    ImGui::PushFont(g_bold, 16.f);
    ImGui::TextColored(kCardboard, "native port");
    ImGui::SameLine();
    ImGui::TextColored(kDim, "%s%s%s", build, version.empty() ? "" : "  ", version.c_str());
    ImGui::PopFont();

    GameState state;
    std::string error, level;
    long code;
    {
        std::lock_guard lock(g_run.m);
        state = g_run.state;
        error = g_run.error;
        code = g_run.exit_code;
        level = g_run.level;
    }
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y + S(112)));
    StatusPill(a, state, level);

    // The check the launcher ran on opening (update_on_start): a newer release is announced once
    // with a toast and then stays on the status line until it's installed. Nothing is installed
    // without the player pressing "Download and install" on the Tools page.
    if (a.release_check_on_start) {
        bool finished;
        {
            std::lock_guard lock(a.release_check.m);
            finished = a.release_check.done && !a.release_check.running;
        }
        if (finished) {
            a.release_check_on_start = false;
            ReleaseInfo r;
            {
                std::lock_guard lock(a.release_m);
                r = a.release;
            }
            if (r.ok && !a.version.empty() && CompareVersions(ParseVersion(r.tag), ParseVersion(a.version)) > 0) {
                a.update_notice = r.tag;
                ToastMsg(a, r.tag + " is out: Tools > Updates installs it.", kGood);
            }
        }
    }

    // the status line (warnings, then a newer release; everything else is the pill and toasts)
    const bool running_other = state != GameState::Running && state != GameState::Starting && a.game_running;
    const bool ok = EssentialsOk(a);
    // (a sign-in test holds the account for a moment: Play waits for it)
    const bool updating = a.update_build.running || a.release_install.running || a.online.test.running;
    const bool can_play = ok && !a.marker && !running_other && !updating && (state == GameState::Idle || state == GameState::Ended) && !a.shot_mode;
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y + S(148)));
    if (a.marker) ImGui::TextColored(kWarn, "A test run has your saves set aside; Play waits until it finishes.");
    else if (updating) ImGui::TextColored(kWarn, "Updating; Play waits until it finishes.");
    else if (!ok) ImGui::TextColored(kWarn, "Something the game needs is missing: Help > Run the setup.");
    else if (state == GameState::Ended && code != 0) {
        ImGui::TextColored(kBad, "%s", error.empty() ? ("The game stopped with an error (code " + std::to_string(code) + ").").c_str() : error.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open the log")) OpenPath(LatestLog(a.p));
    } else if (!a.update_notice.empty() && state != GameState::Running && state != GameState::Starting) {
        ImGui::TextColored(kGood, "%s is out (you have %s).", a.update_notice.c_str(), a.version.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Update")) a.page = Tools;
    }

    // PLAY
    const ImVec2 bs(S(236), S(78));
    ImGui::SetCursorScreenPos(ImVec2(p1.x - bs.x - S(34), p0.y + S(44)));
    const bool busy = state == GameState::Starting || state == GameState::Running;
    const char* label = state == GameState::Starting ? "STARTING" : state == GameState::Running ? "PLAYING" : "PLAY";
    if (PlayButton(label, bs, can_play || (a.shot_mode && ok), busy) && can_play) PlayGame(a);
    if (!a.wizard && !a.tour && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
    Mark(a, "play");
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    ImGui::Dummy(ImVec2(0, 0));
}

void Sidebar(App& a, float width) {
    ImDrawList* bg = ImGui::GetWindowDrawList();
    const ImVec2 s0 = ImGui::GetCursorScreenPos();
    const float hgt = ImGui::GetContentRegionAvail().y;
    bg->AddRectFilled(s0, ImVec2(s0.x + width, s0.y + hgt), Col(kSide));
    Stitches(bg, ImVec2(s0.x + width - S(1), s0.y), ImVec2(s0.x + width - S(1), s0.y + hgt), Col(kStitch, 0.18f), S(8), S(6), S(1.2f));
    ImGui::BeginChild("side", ImVec2(width, 0), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    ImGui::Dummy(ImVec2(0, S(10)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float itemH = S(38);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(3)));
    for (int i = 0; i < kPages; ++i) {
        const bool sel = a.page == i;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##page", ImVec2(width - S(16), itemH))) a.page = static_cast<Page>(i);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) a.page = static_cast<Page>(i);
        const float hv = Ease(ImGui::GetItemID(), hovered ? 1.f : 0.f, 14.f);
        const float sv = Ease(ImGui::GetItemID() + 7, sel ? 1.f : 0.f, 12.f);
        const ImVec2 mn(p.x + S(8), p.y), mx(p.x + width - S(16), p.y + itemH);
        if (sv > 0.01f) {
            dl->AddRectFilled(mn, mx, Col(kCardboard, 0.30f * sv), S(10));
            StitchedRect(dl, mn, mx, Col(kStitch, 0.55f * sv), S(4), S(10));
            dl->AddRectFilled(ImVec2(mn.x, mn.y + S(8)), ImVec2(mn.x + S(4), mx.y - S(8)), Col(kOrange, sv), S(2));
        } else if (hv > 0.01f) {
            dl->AddRectFilled(mn, mx, Col(kCardboard, 0.12f * hv), S(10));
        }
        const ImU32 fg = Col(sel ? kText : ImVec4(kDim.x + (kText.x - kDim.x) * hv, kDim.y + (kText.y - kDim.y) * hv, kDim.z + (kText.z - kDim.z) * hv, 1.f));
        DrawIcon(dl, kPageIcons[i], ImVec2(mn.x + S(24) + S(3) * hv, (mn.y + mx.y) * 0.5f), S(18), sel ? Col(kCardboard) : fg);
        ImGui::PushFont(sel ? g_bold : g_font, 17.f);
        const float th = ImGui::GetTextLineHeight();
        dl->AddText(ImVec2(mn.x + S(46) + S(3) * hv, (mn.y + mx.y - th) * 0.5f), fg, kPageNames[i]);
        ImGui::PopFont();
        Mark(a, (std::string("page_") + kPageKeys[i]).c_str());
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + S(8), ImGui::GetWindowHeight() - S(58)));
    ImGui::SetCursorPosX(S(16));
    ImGui::PushFont(g_font, 14.f);
    ImGui::TextColored(kDim, "L1 / R1 or Ctrl+Tab");
    ImGui::SetCursorPosX(S(16));
    ImGui::TextColored(kDim, "change page");
    ImGui::PopFont();
    ImGui::EndChild();
}

void Frame(App& a) {
    Refresh(a);
    LoadArt(a);
    CommunityFrame(a);
    ArtBackground(a);
    // Leaving the Friends page signs out of shadNet.
    static Page last = Home;
    if (a.page != last) {
        if (last == Friends) StopFriends(a, false);
        if (a.page == Dlc) a.dlc_dirty = true;
        last = a.page;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    Header(a);
    const float side = S(214);
    Sidebar(a, side);
    ImGui::SameLine(0, 0);
    // the page, fading and sliding in when it changes
    const float alpha = PageAlpha(a);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.15f + 0.85f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28), S(18)));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.f - alpha) * S(16));
    ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
    switch (a.page) {
    case Home: PageHome(a); break;
    case Community: PageCommunity(a); break;
    case Online: PageOnline(a); break;
    case Friends: PageFriends(a); break;
    case GamePage: PageGame(a); break;
    case Graphics: PageGraphics(a); break;
    case Controls: PageControls(a); break;
    case Dlc: PageDlc(a); break;
    case Saves: PageSaves(a); break;
    case Controllers: PageControllers(a); break;
    case News: PageNews(a); break;
    case Tools: PageTools(a); break;
    default: PageHelp(a); break;
    }
    ImGui::Dummy(ImVec2(0, S(30)));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    if (a.wizard) Wizard(a);
    else if (a.tour) Tour(a);
    Modals(a);
    OnlineModals(a);
    // Closing while the game runs: asked first (the game keeps running either way).
    if (a.confirm_close) ImGui::OpenPopup("Close the launcher?"), a.confirm_close = false;
    if (ImGui::BeginPopupModal("Close the launcher?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("LittleBigPlanet 3 is still running.");
        Dim("Closing the launcher doesn't close the game; Discord and the play time watch stop until next time.");
        bool ask = a.s.confirm_quit;
        if (ImGui::Checkbox("Ask me this next time", &ask)) a.s.confirm_quit = ask;
        if (BigButton("Close the launcher", ImVec2(S(200), 0), kOrange)) a.close_now = true, ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (BigButton("Keep it open", ImVec2(S(170), 0), kCard)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
    DrawToasts(a);
    if (!a.shot_mode) {
        const std::string ini = ToIni(a.s);
        if (ini != a.saved_ini && SaveSettings(a.s)) a.saved_ini = ini;
    }
}

// ---- the notification area (while playing, if the player wants it) ----
SDL_Tray* g_tray = nullptr;
std::atomic<bool> g_trayShow{false}, g_trayClose{false};

SDL_Surface* IconSurface() {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(2), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!res) return nullptr;
    HGLOBAL mem = LoadResource(nullptr, res);
    const void* data = mem ? LockResource(mem) : nullptr;
    if (!data) return nullptr;
    const Image img = Downscale(LoadImageMemory(data, SizeofResource(nullptr, res)), 64, 64);
    if (!img.ok()) return nullptr;
    SDL_Surface* s = SDL_CreateSurface(img.w, img.h, SDL_PIXELFORMAT_RGBA32);
    if (s) std::memcpy(s->pixels, img.rgba.data(), img.rgba.size());  // pitch == w*4 for RGBA32 at these sizes
    return s;
}

void ShowTray() {
    if (g_tray) return;
    SDL_Surface* icon = IconSurface();
    g_tray = SDL_CreateTray(icon, "LittleBigPlanet 3 Launcher");
    if (icon) SDL_DestroySurface(icon);
    if (!g_tray) return;
    SDL_TrayMenu* menu = SDL_CreateTrayMenu(g_tray);
    SDL_TrayEntry* show = SDL_InsertTrayEntryAt(menu, -1, "Show the launcher", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(show, [](void*, SDL_TrayEntry*) { g_trayShow = true; }, nullptr);
    SDL_TrayEntry* close = SDL_InsertTrayEntryAt(menu, -1, "Close the launcher", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(close, [](void*, SDL_TrayEntry*) { g_trayClose = true; }, nullptr);
}

void HideTray() {
    if (g_tray) SDL_DestroyTray(g_tray), g_tray = nullptr;
}

}  // namespace
}  // namespace launcher::ui

using namespace launcher;
using namespace launcher::ui;

int main(int argc, char** argv) {
    std::string shot_dir, shot_server, print_file, updated;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0) shot_dir = argv[i + 1];
        if (std::strcmp(argv[i], "--shot-server") == 0) shot_server = argv[i + 1];
        if (std::strcmp(argv[i], "--print-launch") == 0) print_file = argv[i + 1];
        if (std::strcmp(argv[i], "--updated") == 0) updated = argv[i + 1];
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
            o << "# Quit Game: written natively to " << (p.root / "user" / "file_edits.bin").string()
              << " before Play, then LBP3_FILE_EDITS and LBP3_QUIT_MENU=1 are added\n";
        for (const auto& c : RunChecks(s, p)) o << (c.ok ? "# ok   " : "# FAIL ") << c.what << ": " << c.detail << "\n";
        return 0;
    }
    const bool shot = !shot_dir.empty();
    // the shot mock: 127.0.0.1 only
    if (!shot_server.empty() && shot_server.rfind("http://127.0.0.1:", 0) != 0) shot_server.clear();

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SetTaskbarIdentity();
    App a;
    a.portable = Portable(LauncherDir());
    a.version = InstalledVersion(LauncherDir());
    if (a.portable && !shot) RemoveOldFiles(LauncherDir());  // what the last update renamed aside
    const bool pads_at_start = !shot && !GameRunning();
    if (!SDL_Init(SDL_INIT_VIDEO | (pads_at_start ? SDL_INIT_GAMEPAD : 0))) {
        MessageBoxA(nullptr, SDL_GetError(), "LBP3 Launcher", MB_ICONERROR);
        return 1;
    }
    a.s = LoadSettings();
    a.saved_ini = ToIni(a.s);
    a.shot_mode = shot;
    a.shot_server = shot_server;
    a.pads_allowed = shot || pads_at_start;
    g_scale = [] {
        const float s = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        return s > 0.f ? s : 1.f;
    }();
    int w = a.s.win_w > 0 ? a.s.win_w : static_cast<int>(1180 * g_scale);
    int h = a.s.win_h > 0 ? a.s.win_h : static_cast<int>(800 * g_scale);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    if (shot) {
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        flags |= SDL_WINDOW_NOT_FOCUSABLE;
        w = static_cast<int>(1180 * g_scale);
        h = static_cast<int>(800 * g_scale);
    }
    a.window = SDL_CreateWindow("LittleBigPlanet 3 Launcher", w, h, flags);
    a.renderer = a.window ? SDL_CreateRenderer(a.window, nullptr) : nullptr;
    if (!a.renderer) {
        MessageBoxA(nullptr, SDL_GetError(), "LBP3 Launcher", MB_ICONERROR);
        return 1;
    }
    SetWindowIconFromResource(a.window);
    SDL_SetRenderVSync(a.renderer, 1);
    SDL_SetWindowMinimumSize(a.window, static_cast<int>(900 * g_scale), static_cast<int>(640 * g_scale));
    if (shot) {
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
    if (const float ws = SDL_GetWindowDisplayScale(a.window); ws > 0.f) g_scale = ws;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | (shot ? 0 : ImGuiConfigFlags_NavEnableGamepad);
    LoadFonts();
    ApplyStyle(g_scale);
    ImGui_ImplSDL3_InitForSDLRenderer(a.window, a.renderer);
    ImGui_ImplSDLRenderer3_Init(a.renderer);
    if (!pads_at_start) ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);

    Refresh(a, true);
    if (!a.p.root.empty()) g_shadnetLogPath = (a.p.root / "analysis" / "launcher_shadnet_log.txt").string();
    a.dlc = ListDlc(a.p.dlc_dir);
    a.dlc_dirty = false;
    StartStats(a);
    LoadNews(a);
    if (!a.s.setup_done && !shot) a.wizard = true;
    else if (!a.s.tour_done && !shot) a.tour = true;
    if (!updated.empty()) ToastMsg(a, "Updated to " + updated + ".", kGood);
    if (a.s.update_on_start && !shot) {
        if (DevCheckout(a)) StartUpdateCheck(a);
        else {
            a.release_check_on_start = true;
            StartReleaseCheck(a);
        }
    }

    // --shot: each page, the setup's steps, the tour, then a few moments in motion
    struct Shot {
        std::string name;
        std::function<void()> setup;
        int frames;
        std::function<void()> act;  // input for the shot, a few frames in (once its windows exist)
        int act_frame = 0;
    };
    std::vector<Shot> shots;
    if (shot) {
        for (int i = 0; i < kPages; ++i)
            shots.push_back({kPageKeys[i], [&a, i] { a.wizard = a.tour = false, a.page = static_cast<Page>(i); }, i == Community || i == Online ? 150 : 50});
        for (int i = 0; i < 7; ++i) shots.push_back({"setup_" + std::to_string(i), [&a, i] { a.page = Home, a.tour = false, a.wizard = true, a.wizard_step = i; }, 30});
        for (int i = 0; i < 5; ++i) shots.push_back({"tour_" + std::to_string(i), [&a, i] { a.wizard = false, a.tour = true, a.tour_step = i; }, 30});
        shots.push_back({"motion_play_hover", [&a] {
                             a.wizard = a.tour = false;
                             a.page = Home;
                             if (a.marks.count("play")) {
                                 const auto& [mn, mx] = a.marks["play"];
                                 ImGui::GetIO().AddMousePosEvent((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
                             }
                         },
                         40});
        shots.push_back({"motion_page_change", [&a] { a.page = Saves; }, 5});
        // a click on the first level card (its popup), and keyboard navigation (the focus ring)
        shots.push_back({"motion_community_click", [&a] { a.page = Community; }, 50,
                         [&a] {
                             if (!a.marks.count("level_0")) return;
                             const auto& [mn, mx] = a.marks["level_0"];
                             ImGuiIO& io = ImGui::GetIO();
                             io.AddMousePosEvent((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
                             io.AddMouseButtonEvent(0, true);
                             io.AddMouseButtonEvent(0, false);
                         },
                         20});
        shots.push_back({"motion_keyboard_nav", [&a] {
                             a.community.selected = -1;
                             if (ImGui::GetCurrentContext()->OpenPopupStack.Size) ImGui::ClosePopupToLevel(0, true);  // the level's popup
                             a.page = Graphics;
                             ImGuiIO& io = ImGui::GetIO();
                             io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                             for (int i = 0; i < 4; ++i) io.AddKeyEvent(ImGuiKey_DownArrow, true), io.AddKeyEvent(ImGuiKey_DownArrow, false);
                         },
                         40});
        shots.push_back({"motion_toasts", [&a] {
                             a.page = Home;
                             ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                             ToastMsg(a, "Saves backed up as manual_20261113_1200.", kGood);
                             ToastMsg(a, "Friend request sent to Pal_1", kInfo);
                         },
                         20});
    }
    size_t shot_index = 0;
    int shot_frames = 0;
    if (shot && !shots.empty()) shots[0].setup();

    bool running = true, minimized_for_game = false;
    GameState last_state = GameState::Idle;
    Uint64 last_game_poll = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // the controls editor waits for a key or pad button: it gets it, not the window
            if (!a.controls.capture_key.empty() && (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)) {
                const bool pad = a.controls.capture_key.rfind("pad_", 0) == 0;
                if (e.type == SDL_EVENT_KEY_DOWN && !pad) a.controls.captured = SDL_GetScancodeName(e.key.scancode);
                if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad) {
                    const char* n = input::actions::PadButtonName(static_cast<SDL_GamepadButton>(e.gbutton.button));
                    if (n) a.controls.captured = n;
                }
                if (e.type == SDL_EVENT_KEY_DOWN && pad && e.key.key == SDLK_ESCAPE) a.controls.captured = "\x1b";
                continue;
            }
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(a.window))) {
                if (GameBusy(a) && a.s.confirm_quit && !shot) {
                    a.confirm_close = true;  // asked in the window, so it comes back first (minimised while playing)
                    SDL_ShowWindow(a.window);
                    SDL_RestoreWindow(a.window);
                    SDL_RaiseWindow(a.window);
                } else {
                    running = false;
                }
            }
            if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                const float ns = SDL_GetWindowDisplayScale(a.window);
                if (ns > 0.f && std::fabs(ns - g_scale) > 0.01f) g_scale = ns, ApplyStyle(g_scale);
            }
            if (!shot && (e.type == SDL_EVENT_WINDOW_MOVED || e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_MAXIMIZED ||
                          e.type == SDL_EVENT_WINDOW_RESTORED)) {
                const SDL_WindowFlags f = SDL_GetWindowFlags(a.window);
                if (!(f & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))) {
                    a.s.win_max = (f & SDL_WINDOW_MAXIMIZED) != 0;
                    if (!a.s.win_max) {
                        SDL_GetWindowPosition(a.window, &a.s.win_x, &a.s.win_y);
                        SDL_GetWindowSize(a.window, &a.s.win_w, &a.s.win_h);
                    }
                }
            }
            if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && a.page != Controllers && !a.wizard && !a.tour) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) a.page = static_cast<Page>((a.page + kPages - 1) % kPages);
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) a.page = static_cast<Page>((a.page + 1) % kPages);
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_TAB && (e.key.mod & SDL_KMOD_CTRL) && !a.wizard && !a.tour)
                a.page = static_cast<Page>((a.page + ((e.key.mod & SDL_KMOD_SHIFT) ? kPages - 1 : 1)) % kPages);
        }
        if (a.close_now) running = false;
        if (g_trayShow.exchange(false)) {
            SDL_ShowWindow(a.window);
            SDL_RaiseWindow(a.window);
            a.hidden_to_tray = false;
            HideTray();
        }
        if (g_trayClose.exchange(false)) {
            SDL_ShowWindow(a.window);
            a.hidden_to_tray = false;
            HideTray();
            if (GameBusy(a) && a.s.confirm_quit) a.confirm_close = true;
            else running = false;
        }

        // The game, started from here or not: the launcher lets go of the controllers completely
        // while it runs, signs out of Friends (the game signs in itself), and comes back after.
        const GameState state = CurrentState();
        const bool friendsOn = a.friends.session.Active();
        const Uint64 pollEvery = friendsOn ? 500 : 2000;  // faster while signed in to shadNet
        if (!shot && SDL_GetTicks() - last_game_poll > pollEvery) {
            a.game_running = GameRunning();
            last_game_poll = SDL_GetTicks();
        }
        const bool playing = state == GameState::Starting || state == GameState::Running || a.game_running;
        if (playing && friendsOn) StopFriends(a, false);
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
        if (state == GameState::Running && !minimized_for_game && (a.s.minimize_while_playing || a.s.tray_while_playing)) {
            if (a.s.tray_while_playing) {
                ShowTray();
                SDL_HideWindow(a.window);
                a.hidden_to_tray = true;
            } else {
                SDL_MinimizeWindow(a.window);
            }
            minimized_for_game = true;
        } else if (state != GameState::Running && state != GameState::Starting && minimized_for_game) {
            HideTray();
            SDL_ShowWindow(a.window);
            SDL_RestoreWindow(a.window);
            SDL_RaiseWindow(a.window);
            a.hidden_to_tray = false;
            minimized_for_game = false;
        }
        // nothing to draw while minimised or in the tray (the watcher thread keeps the log and Discord)
        if (SDL_GetWindowFlags(a.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) {
            SDL_Delay(100);
            continue;
        }
        if (a.page == Controllers) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
            bool circle = false;
            for (auto& pv : a.pads)
                if (pv->pad && SDL_GetGamepadButton(pv->pad, SDL_GAMEPAD_BUTTON_EAST)) circle = true;
            const double t = SDL_GetTicks() / 1000.0;
            if (!circle) a.circle_held = 0;
            else if (a.circle_held == 0) a.circle_held = t;
            else if (t - a.circle_held > 1.0) a.page = Home, a.circle_held = 0;
        } else if (!shot) {
            if (a.pads_allowed && a.controls.capture_key.empty()) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
            else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
            const bool padCapture = a.controls.capture_key.rfind("pad_", 0) == 0;
            if (!a.pads.empty() && !padCapture) ClosePads(a);
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (shot) io.DeltaTime = 1.f / 60.f;  // a fixed clock: the same pictures every run
        a.dt = io.DeltaTime;
        a.time += io.DeltaTime;
        ImGui::NewFrame();
        Frame(a);
        ImGui::Render();
        SDL_SetRenderDrawColor(a.renderer, 23, 19, 15, 255);
        SDL_RenderClear(a.renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), a.renderer);
        const bool shot_ready = a.news_ready && !a.stats_job.running && !a.community.job.running && !a.online.ping.running &&
                                !a.community.icon_worker;
        if (shot && shot_index < shots.size() && shots[shot_index].act && shot_frames == shots[shot_index].act_frame) shots[shot_index].act();
        if (shot && shot_index < shots.size() && (shot_ready || shot_frames > 600) && ++shot_frames >= shots[shot_index].frames) {
            SaveShot(a.renderer, shot_dir + "\\launcher_" + shots[shot_index].name + ".bmp");
            shot_frames = 0;
            if (++shot_index < shots.size()) shots[shot_index].setup();
            else running = false;
        }
        SDL_RenderPresent(a.renderer);
    }
    {
        std::lock_guard lock(g_run.m);
        if (g_run.state == GameState::Running) g_run.state = GameState::Ended;  // stops its watcher thread; the game keeps running
    }
    if (!shot) SaveSettings(a.s);
    HideTray();
    StopFriends(a, true);
    Job* jobs[] = {&a.file_check, &a.dlc_install, &a.update_check, &a.update_build, &a.stats_job, &a.discord_test, &a.release_check,
                   &a.release_install, &a.community.job};
    for (Job* j : jobs) j->cancel = true;
    for (int i = 0; i < 100; ++i) {
        bool busy = false;
        for (Job* j : jobs) busy |= j->running.load();
        if (!busy) break;
        Sleep(30);
    }
    ClosePads(a);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(a.renderer);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    CoUninitialize();
    ExitProcess(0);  // background threads may still hold the App on this stack
}
