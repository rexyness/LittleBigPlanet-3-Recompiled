// LBP3Launcher: a small window for starting the native LittleBigPlanet 3 with its settings, backing
// up and restoring saves, and finding logs, instead of editing play.bat. SDL3 + Dear ImGui, styled
// like the in-game menu (ui/menu_overlay.cpp). Our own code; LBPPC's launcher was only an idea
// (its code is GPL-3.0 and not reused here).
//
//   LBP3Launcher.exe              the window
//   LBP3Launcher.exe --shot DIR   draws each page once into DIR\launcher_<page>.bmp and exits
//                                 (for the docs; no buttons are pressed, nothing is started)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "launcher_account.h"
#include "launcher_settings.h"
#include "launcher_sys.h"

using namespace launcher;

namespace {

// The in-game menu's palette (ui/menu_overlay.cpp).
const ImVec4 kPanel = ImVec4(18 / 255.f, 22 / 255.f, 34 / 255.f, 1.f);
const ImVec4 kPanel2 = ImVec4(26 / 255.f, 32 / 255.f, 48 / 255.f, 1.f);
const ImVec4 kEdge = ImVec4(90 / 255.f, 120 / 255.f, 200 / 255.f, 200 / 255.f);
const ImVec4 kSel = ImVec4(52 / 255.f, 96 / 255.f, 196 / 255.f, 235 / 255.f);
const ImVec4 kText = ImVec4(232 / 255.f, 236 / 255.f, 245 / 255.f, 1.f);
const ImVec4 kDim = ImVec4(150 / 255.f, 158 / 255.f, 175 / 255.f, 1.f);
const ImVec4 kWarn = ImVec4(255 / 255.f, 196 / 255.f, 92 / 255.f, 1.f);
const ImVec4 kGood = ImVec4(120 / 255.f, 214 / 255.f, 140 / 255.f, 1.f);
const ImVec4 kBad = ImVec4(255 / 255.f, 110 / 255.f, 110 / 255.f, 1.f);
const ImVec4 kPlay = ImVec4(46 / 255.f, 150 / 255.f, 86 / 255.f, 1.f);

ImFont* g_font = nullptr;
ImFont* g_bold = nullptr;

void ApplyStyle(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 0.f;
    st.FrameRounding = 6.f;
    st.GrabRounding = 6.f;
    st.TabRounding = 6.f;
    st.PopupRounding = 8.f;
    st.ChildRounding = 8.f;
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    st.WindowPadding = ImVec2(18, 14);
    st.FrameBorderSize = 1.f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = kPanel;
    c[ImGuiCol_ChildBg] = kPanel2;
    c[ImGuiCol_PopupBg] = kPanel2;
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
    c[ImGuiCol_Tab] = ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f);
    c[ImGuiCol_TabHovered] = kSel;
    c[ImGuiCol_TabSelected] = kSel;
    c[ImGuiCol_SliderGrab] = kSel;
    c[ImGuiCol_SliderGrabActive] = ImVec4(70 / 255.f, 120 / 255.f, 220 / 255.f, 1.f);
    c[ImGuiCol_Separator] = ImVec4(kEdge.x, kEdge.y, kEdge.z, 0.35f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    st.ScaleAllSizes(scale);
    st.FontSizeBase = 17.f;
    st.FontScaleDpi = scale;
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
    ImGui::TextColored(ImVec4(170 / 255.f, 196 / 255.f, 1.f, 1.f), "%s", text);
    ImGui::PopFont();
    ImGui::Separator();
}

// A text field bound to a std::string.
bool InputString(const char* label, std::string& value, const char* hint = nullptr, float width = 0.f) {
    char buf[1024];
    std::snprintf(buf, sizeof buf, "%s", value.c_str());
    if (width > 0.f) ImGui::SetNextItemWidth(width);
    const bool changed = hint ? ImGui::InputTextWithHint(label, hint, buf, sizeof buf) : ImGui::InputText(label, buf, sizeof buf);
    if (changed) value = buf;
    return changed;
}

// A file or folder chooser (Windows' own dialog). Empty when cancelled.
std::string PickPath(bool folder, const wchar_t* title) {
    std::string out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0));
    dlg->SetTitle(title);
    if (!folder) {
        COMDLG_FILTERSPEC spec[] = {{L"lbp3.exe", L"lbp3.exe"}, {L"Programs", L"*.exe"}};
        dlg->SetFileTypes(2, spec);
    }
    if (SUCCEEDED(dlg->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                out.assign(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
                WideCharToMultiByte(CP_UTF8, 0, path, -1, out.data(), n, nullptr, nullptr);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return out;
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

// ---- the game process, started on a worker thread ----
enum class GameState { Idle, Starting, Running, Ended };

struct Run {
    std::mutex m;
    GameState state = GameState::Idle;
    std::uintptr_t process = 0;
    long exit_code = 0;
    std::string note;   // Quit Game's setup line, or the start error
    std::string error;
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
    for (const auto& [k, v] : env) summary += " " + k + "=" + (k == "LBP3_ONLINE_NAME" ? "<name>" : v);
    AppendLauncherLog(p, summary);
    if (!note.empty()) AppendLauncherLog(p, note);
    const std::uintptr_t h = StartGame(p, env, plan.args, &error);
    std::lock_guard lock(g_run.m);
    g_run.note = note;
    if (!h) {
        g_run.state = GameState::Ended;
        g_run.exit_code = -1;
        g_run.error = error;
        AppendLauncherLog(p, "start failed: " + error);
        return;
    }
    g_run.process = h;
    g_run.state = GameState::Running;
}

// ---- account creation, on a worker thread ----
struct AccountJob {
    std::mutex m;
    bool busy = false, done = false;
    AccountResult result;
    std::string name, server;
};
AccountJob g_account;

void AccountWorker(std::string server, std::string name, Paths p) {
    AccountResult r = CreateRefreshAccount(server, name);
    if (r.ok && !p.root.empty()) {
        // The password goes to a file only on this PC (the report zip leaves it out).
        const fs::path file = p.root / "user" / "config" / "online_account.txt";
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        std::ofstream(file, std::ios::app) << "# " << NowStamp() << "\nserver=" << server << "\nname=" << name
                                           << "\npassword=" << r.password << "\n\n";
        AppendLauncherLog(p, "online account created on " + server + " (the password is in user\\config\\online_account.txt)");
    }
    std::lock_guard lock(g_account.m);
    g_account.result = std::move(r);
    g_account.busy = false;
    g_account.done = true;
}

struct Ui {
    Settings s;
    std::string saved_ini;
    Paths p;
    std::vector<Check> checks;
    std::vector<Backup> backups;
    bool game_running = false;  // any lbp3.exe (refreshed with the rest)
    bool marker = false;        // analysis\savedata_live_is_test
    double last_refresh = -10.0;
    std::string banner;  // a one-line result shown under the header
    ImVec4 banner_color = kDim;
    int restore_index = -1;
    bool confirm_account = false;
    bool show_password = false;
    int force_tab = -1;  // --shot
    bool shot_mode = false;
};

void Refresh(Ui& ui, bool now = false) {
    const double t = ImGui::GetTime();
    if (!now && t - ui.last_refresh < 1.5) return;
    ui.last_refresh = t;
    ui.p = ResolvePaths(ui.s);
    ui.checks = RunChecks(ui.s, ui.p);
    ui.backups = ListBackups(ui.p);
    ui.game_running = GameRunning();
    ui.marker = TestMarkerSet(ui.p);
}

bool AllChecksOk(const Ui& ui) {
    for (const auto& c : ui.checks)
        if (!c.ok && (c.what == "lbp3.exe" || c.what == "Game files (the dump)" || c.what == "Game folder (saves, logs)")) return false;
    return true;
}

void Banner(Ui& ui, const std::string& text, ImVec4 color) {
    ui.banner = text;
    ui.banner_color = color;
}

// ---- pages ----
void PageGame(Ui& ui) {
    Settings& s = ui.s;
    Heading("Which game build");
    const char* builds[] = {"This launcher's build (next to LBP3Launcher.exe)", "Best of both worlds (lbp3-wt\\best)",
                            "Port (the game folder's own build, as play.bat)", "Another lbp3.exe..."};
    int b = static_cast<int>(s.build);
    ImGui::SetNextItemWidth(520);
    if (ImGui::Combo("##build", &b, builds, IM_ARRAYSIZE(builds))) s.build = static_cast<Build>(b), Refresh(ui, true);
    Help("Which lbp3.exe to start. \"Best of both worlds\" is the branch with every new feature; \"Port\" is the "
         "one play.bat starts. Settings a build doesn't know are simply ignored by it.");
    if (s.build == Build::Custom) {
        if (InputString("##exe", s.exe_path, "path to lbp3.exe", 520)) Refresh(ui, true);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            const std::string picked = PickPath(false, L"Choose lbp3.exe");
            if (!picked.empty()) s.exe_path = picked, Refresh(ui, true);
        }
    }
    ImGui::TextDisabled("%s", ui.p.exe.string().c_str());

    Heading("Starting");
    Toggle("Skip the logos and the intro movie", &s.skip_intro, "The PlayStation and studio logos and the live-action intro.");
    Toggle("Minimize the launcher while playing", &s.minimize_while_playing, "It comes back when the game closes.");

    Heading("Language");
    const auto& langs = Languages();
    int cur = 0;
    for (int i = 0; i < static_cast<int>(langs.size()); ++i)
        if (s.language == langs[i].code) cur = i;
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("##lang", langs[cur].name)) {
        for (int i = 0; i < static_cast<int>(langs.size()); ++i)
            if (ImGui::Selectable(langs[i].name, i == cur)) s.language = langs[i].code;
        ImGui::EndCombo();
    }
    Help("The US edition normally shows English or French only; any other choice here also unlocks the "
         "languages it hides (from LBPPC's finding).");

    Heading("Menus");
    Toggle("In-game menu (Insert, or L3+R3 together)", &s.menu,
           "Frame rate and latency, frame generation on/off, volume, a small stats corner, and Quit.");
    ImGui::BeginDisabled(!s.menu);
    Toggle("Show the stats corner from the start", &s.stats, "The in-game menu can turn it on and off too.");
    ImGui::EndDisabled();
    Toggle("\"Quit Game\" in the game's pause menu", &s.quit_menu,
           "Replaces the Good Grief! button, which is greyed out offline; press it twice. Needs Python for its "
           "setup step (LBPPC's script edit); the game files are never changed.");

    Heading("LBP1 and LBP2 items");
    Toggle("Import the LBP1/LBP2 items once (the old \"My Unlocks\")", &s.my_unlocks,
           "Runs the game's own import the first time you open the start menu (Options).");
    if (s.my_unlocks) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped(
            "On the first Options press the import screen appears: wait about 2 minutes, don't press Circle, then "
            "press Cross on \"successfully imported\". Then close the game and switch this off again. Back up your "
            "saves first (Saves page).");
        ImGui::PopStyleColor();
    }
}

void PageGraphics(Ui& ui) {
    Settings& s = ui.s;
    Heading("Resolution");
    int r = s.res == 1080 ? 1 : 0;
    if (ImGui::RadioButton("2560x1440 (sharper; the game draws at 1440p)", &r, 0)) s.res = 1440;
    if (ImGui::RadioButton("1920x1080 (the PS4's own, stretched to the window)", &r, 1)) s.res = 1080;
    ImGui::BeginDisabled(s.res != 1440);
    Toggle("Finer glow at 1440p", &s.bloom_1440, "The glow (bloom) is drawn at the higher resolution too (from LBPPC).");
    ImGui::EndDisabled();

    Heading("Frame generation");
    Toggle("60 frames a second on screen (the game itself stays at 30)", &s.framegen,
           "A generated frame between each pair of real ones. About 17 ms more input lag.");
    ImGui::BeginDisabled(!s.framegen);
    Toggle("Keep in-world text readable (the map's \"Loading...\" ring)", &s.fg_text,
           "Generated frames keep thin bright text sharp; about 0.4 ms of GPU per generated frame.");
    ImGui::EndDisabled();

    Heading("Shaders and startup");
    const char* caches[] = {"Automatic (its own copy for builds other than the game folder's)",
                            "This build's own copy (user\\shader_cache_best)", "The normal one (user\\shader_cache)"};
    int c = static_cast<int>(s.cache);
    ImGui::SetNextItemWidth(520);
    if (ImGui::Combo("Shader cache", &c, caches, IM_ARRAYSIZE(caches))) s.cache = static_cast<CacheChoice>(c), Refresh(ui, true);
    Help("Where compiled shaders are kept. The best-of-both-worlds build keeps its own copy, so it never mixes "
         "with play.bat's.");
    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("Startup threads (0 = all cores but one)", &s.pipeline_threads, 0, 32);
    Help("How many CPU cores rebuild the cached shaders at startup (from LBPPC). 1 = one at a time, as play.bat.");
    Toggle("Save the graphics driver's own shader cache", &s.vk_pipeline_cache,
           "Later startups rebuild faster; the first start with a new cache file takes 6-12 s longer, once.");

    Heading("Logs");
    int flips = s.status_flips == 300 ? 1 : 0;
    if (ImGui::RadioButton("Status lines every second (as play.bat)", &flips, 0)) s.status_flips = 30;
    ImGui::SameLine();
    if (ImGui::RadioButton("Every 10 seconds (smaller logs)", &flips, 1)) s.status_flips = 300;
}

void PageControls(Ui& ui) {
    Settings& s = ui.s;
    Heading("Motion");
    Toggle("Filtered motion controls", &s.motion_filter,
           "The gyro's slow drift is learned away while the pad is still, and the tilt settles towards gravity. "
           "Off = the raw sensor values, as play.bat.");
    Toggle("Tilt with the number pad (4/6 and 8/2)", &s.tilt_keys, "For pads without motion sensors.");
    Heading("Touch pad");
    Toggle("The mouse as the touch pad", &s.mouse_touch,
           "For touch Create mode without a DualShock 4 or DualSense touch pad. F7 captures and releases the "
           "mouse; click = touch; the wheel pinches.");
    ImGui::Spacing();
    ImGui::TextDisabled("Buttons are set in user\\config\\input.ini.");
}

void PageOnline(Ui& ui) {
    Settings& s = ui.s;
    Heading("Online play (off unless switched on here)");
    ImGui::TextWrapped(
        "LittleBigPlanet 3's own servers are closed; online play uses a community server. Nothing is sent "
        "anywhere unless you switch this on and start the game, or press Create account.");
    Toggle("Online on", &s.online, "Signs in to the server below when the game starts.");
    InputString("Server", s.online_server, "for example play.lbppc.com", 360);
    Help("The address of a community LittleBigPlanet server (Refresh). For LBPPC's public server, ask them "
         "first. A server on this PC: 127.0.0.1:10061.");
    InputString("Online name", s.online_name, "3-16 letters, digits, - or _", 360);
    const std::string server = NormalizeServer(s.online_server);
    const bool name_ok = ValidOnlineName(s.online_name);
    if (!s.online_server.empty() && server.empty()) ImGui::TextColored(kBad, "That isn't a server address.");
    else if (!server.empty()) ImGui::TextDisabled("The game will use %s", server.c_str());
    if (!s.online_name.empty() && !name_ok) ImGui::TextColored(kBad, "The online name needs 3-16 letters, digits, - or _.");
    if (s.online && (server.empty() || !name_ok))
        ImGui::TextColored(kWarn, "Online stays off until both the server and the name are filled in.");

    Heading("Account");
    bool busy;
    {
        std::lock_guard lock(g_account.m);
        busy = g_account.busy;
    }
    ImGui::BeginDisabled(server.empty() || !name_ok || busy || ui.shot_mode);
    if (ImGui::Button(busy ? "Creating..." : "Create account")) ui.confirm_account = true;
    ImGui::EndDisabled();
    Help("Registers your online name on the server (its levels and profiles part) with a strong random "
         "password, which is shown once and saved in user\\config\\online_account.txt.");
    ImGui::BeginDisabled(true);
    ImGui::Button("Create multiplayer account (shadNet)");
    ImGui::EndDisabled();
    Help("Friends, invites and joining other players need shadNet; the port's shadNet layer is still being "
         "built. Playing community levels alone only needs the account above.");
    std::error_code ec;
    if (!ui.p.root.empty() && fs::exists(ui.p.root / "user" / "config" / "online_account.txt", ec)) {
        if (ImGui::SmallButton("Open the saved account file")) OpenPath(ui.p.root / "user" / "config" / "online_account.txt");
    }
}

void PageSaves(Ui& ui) {
    Heading("Your saves");
    ImGui::TextDisabled("%s", ui.p.saves.string().c_str());
    const bool running = ui.game_running;
    const bool marker = ui.marker;
    ImGui::Text("Last backup: %s", ui.backups.empty() ? "none" : (When(ui.backups.front().time) + "  (" + ui.backups.front().name + ")").c_str());
    if (running) ImGui::TextColored(kWarn, "Close the game first: saves are only copied while it isn't running.");
    if (marker) ImGui::TextColored(kWarn, "A test run has your saves set aside right now; wait until it finishes.");
    ImGui::BeginDisabled(running || marker || ui.shot_mode);
    if (ImGui::Button("Back up saves now")) {
        std::string error;
        const std::string name = BackupSaves(ui.p, "manual", &error);
        if (name.empty()) Banner(ui, "Backup failed: " + error, kBad);
        else {
            Banner(ui, "Saves backed up as " + name, kGood);
            AppendLauncherLog(ui.p, "saves backed up as " + name);
        }
        Refresh(ui, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open the saves folder")) OpenPath(ui.p.saves);
    ImGui::SameLine();
    if (ImGui::Button("Open the backups folder")) OpenPath(ui.p.backups);

    Heading("Restore a backup");
    ImGui::TextWrapped("Restoring first backs up the saves you have now, so nothing is lost.");
    if (ImGui::BeginTable("backups", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetContentRegionAvail().y - 8))) {
        ImGui::TableSetupColumn("Backup", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(ui.backups.size()); ++i) {
            const Backup& bk = ui.backups[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(bk.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", When(bk.time).c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::BeginDisabled(running || marker || ui.shot_mode);
            if (ImGui::SmallButton("Restore")) ui.restore_index = i;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void PageHelp(Ui& ui) {
    Heading("Checks");
    for (const auto& c : ui.checks) {
        ImGui::TextColored(c.ok ? kGood : kWarn, c.ok ? "OK " : "!  ");
        ImGui::SameLine();
        ImGui::TextUnformatted(c.what.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", c.detail.c_str());
    }
    Heading("Folders and logs");
    if (ImGui::Button("Game folder")) OpenPath(ui.p.root);
    ImGui::SameLine();
    if (ImGui::Button("Saves")) OpenPath(ui.p.saves);
    ImGui::SameLine();
    if (ImGui::Button("DLC folder")) OpenPath(ui.p.dlc_dir);
    ImGui::SameLine();
    const fs::path latest = LatestLog(ui.p);
    ImGui::BeginDisabled(latest.empty());
    if (ImGui::Button("Latest log")) OpenPath(latest);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("All logs")) OpenPath(ui.p.root / "analysis" / "play_logs");
    ImGui::BeginDisabled(ui.shot_mode);
    if (ImGui::Button("Make a report zip")) {
        std::string error;
        const fs::path zip = MakeReport(ui.s, ui.p, &error);
        if (zip.empty()) Banner(ui, "Report failed: " + error, kBad);
        else {
            Banner(ui, "Report saved: " + zip.string(), kGood);
            OpenPath(zip.parent_path());
        }
    }
    ImGui::EndDisabled();
    Help("The latest logs, these settings and your PC's basic specs, for when something goes wrong. Your saves "
         "and online password are not included.");

    Heading("Game folder");
    ImGui::TextWrapped("The folder with user\\, analysis\\ and game_dir.txt. Leave it empty to find it automatically.");
    if (InputString("##root", ui.s.game_root, "found automatically", 520)) Refresh(ui, true);
    ImGui::SameLine();
    if (ImGui::Button("Browse...##root")) {
        const std::string picked = PickPath(true, L"Choose the game folder");
        if (!picked.empty()) ui.s.game_root = picked, Refresh(ui, true);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Settings: %s", SettingsFile(FindRoot(LauncherDir(), "")).string().c_str());
    ImGui::TextDisabled("Ideas from LBPPC (github.com/emurphy04/LBPPC); this launcher is our own code.");
}

void Header(Ui& ui) {
    ImGui::PushFont(g_bold, 28.f);
    ImGui::TextUnformatted("LittleBigPlanet 3");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("  native port launcher");

    GameState state;
    std::string note, error;
    long code;
    {
        std::lock_guard lock(g_run.m);
        if (g_run.state == GameState::Running) {
            const long c = GameExitCode(g_run.process);
            if (c != -1) {
                CloseGameHandle(g_run.process);
                g_run.process = 0;
                g_run.exit_code = c;
                g_run.state = GameState::Ended;
                AppendLauncherLog(ui.p, "the game ended with code " + std::to_string(c));
            }
        }
        state = g_run.state;
        note = g_run.note;
        error = g_run.error;
        code = g_run.exit_code;
    }
    const bool running_any = state != GameState::Running && state != GameState::Starting && ui.game_running;
    const bool marker = ui.marker;
    const bool ok = AllChecksOk(ui);

    // The Play button, on the right.
    const float bw = 200.f * ImGui::GetStyle().FontScaleDpi, bh = 52.f * ImGui::GetStyle().FontScaleDpi;
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - bw);
    ImGui::SetCursorPosY(ImGui::GetStyle().WindowPadding.y);
    const bool can_play = ok && !marker && !running_any && (state == GameState::Idle || state == GameState::Ended) && !ui.shot_mode;
    ImGui::PushStyleColor(ImGuiCol_Button, can_play ? kPlay : ImVec4(0.25f, 0.27f, 0.32f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(60 / 255.f, 180 / 255.f, 104 / 255.f, 1.f));
    ImGui::PushFont(g_bold, 24.f);
    ImGui::BeginDisabled(!can_play);
    const char* label = state == GameState::Starting ? "Starting..." : state == GameState::Running ? "Playing" : "PLAY";
    if (ImGui::Button(label, ImVec2(bw, bh))) {
        SaveSettings(ui.s);
        ui.saved_ini = ToIni(ui.s);
        Refresh(ui, true);
        {
            std::lock_guard lock(g_run.m);
            g_run.state = GameState::Starting;
            g_run.note.clear();
            g_run.error.clear();
        }
        std::thread(LaunchWorker, ui.s, ui.p).detach();
        Banner(ui, "Starting the game...", kDim);
    }
    ImGui::EndDisabled();
    ImGui::PopFont();
    ImGui::PopStyleColor(2);

    // The status line.
    ImGui::SetCursorPosY(ImGui::GetStyle().WindowPadding.y + 40.f * ImGui::GetStyle().FontScaleDpi);
    if (marker) ImGui::TextColored(kWarn, "A test run has your saves set aside; Play waits until it finishes.");
    else if (running_any) ImGui::TextColored(kWarn, "LittleBigPlanet 3 is already running.");
    else if (!ok) ImGui::TextColored(kWarn, "Something is missing: see the Help page.");
    else if (state == GameState::Running) ImGui::TextColored(kGood, "Playing. %s", note.c_str());
    else if (state == GameState::Ended && code != 0) {
        ImGui::TextColored(kBad, "%s", error.empty() ? ("The game stopped with an error (code " + std::to_string(code) + ").").c_str() : error.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Open the log")) OpenPath(LatestLog(ui.p));
    } else if (!ui.banner.empty()) ImGui::TextColored(ui.banner_color, "%s", ui.banner.c_str());
    else ImGui::TextDisabled("Ready. %s", ui.s.online && !NormalizeServer(ui.s.online_server).empty() && ValidOnlineName(ui.s.online_name) ? "Online on." : "Offline.");
    ImGui::SetCursorPosY(ImGui::GetStyle().WindowPadding.y + bh + 8.f);
}

void Modals(Ui& ui) {
    if (ui.restore_index >= 0) ImGui::OpenPopup("Restore saves?");
    if (ImGui::BeginPopupModal("Restore saves?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ui.restore_index < 0 || ui.restore_index >= static_cast<int>(ui.backups.size())) {
            ui.restore_index = -1;
            ImGui::CloseCurrentPopup();
        } else {
            const Backup& bk = ui.backups[ui.restore_index];
            ImGui::Text("Replace your saves with \"%s\" (%s)?", bk.name.c_str(), When(bk.time).c_str());
            ImGui::TextDisabled("Your current saves are backed up first.");
            if (ImGui::Button("Restore", ImVec2(140, 0))) {
                std::string error, safety;
                if (GameRunning()) Banner(ui, "Close the game first.", kWarn);
                else if (RestoreBackup(ui.p, bk, &error, &safety)) {
                    Banner(ui, "Restored " + bk.name + (safety.empty() ? "" : " (your previous saves: " + safety + ")"), kGood);
                    AppendLauncherLog(ui.p, "restored " + bk.name + ", previous saves kept as " + safety);
                } else Banner(ui, "Restore failed: " + error, kBad);
                ui.restore_index = -1;
                Refresh(ui, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(140, 0))) {
                ui.restore_index = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (ui.confirm_account) ImGui::OpenPopup("Create account?"), ui.confirm_account = false;
    if (ImGui::BeginPopupModal("Create account?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string server = NormalizeServer(ui.s.online_server);
        ImGui::Text("Register \"%s\" on %s?", ui.s.online_name.c_str(), server.c_str());
        ImGui::TextDisabled("Your online name is sent to that server; the password is made here and only its hash is sent.");
        if (ImGui::Button("Create", ImVec2(140, 0))) {
            {
                std::lock_guard lock(g_account.m);
                g_account.busy = true;
                g_account.done = false;
                g_account.name = ui.s.online_name;
                g_account.server = server;
            }
            std::thread(AccountWorker, server, ui.s.online_name, ui.p).detach();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    bool done;
    {
        std::lock_guard lock(g_account.m);
        done = g_account.done;
    }
    if (done) ImGui::OpenPopup("Account");
    if (ImGui::BeginPopupModal("Account", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::lock_guard lock(g_account.m);
        g_account.done = false;
        const AccountResult& r = g_account.result;
        ImGui::TextColored(r.ok ? kGood : kBad, "%s", r.message.c_str());
        if (r.ok) {
            ImGui::Text("Password (for the server's website):");
            ImGui::PushFont(g_bold, 20.f);
            ImGui::TextUnformatted(r.password.c_str());
            ImGui::PopFont();
            ImGui::TextDisabled("Saved in user\\config\\online_account.txt. It isn't shown again.");
        }
        if (ImGui::Button("OK", ImVec2(140, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Frame(Ui& ui) {
    Refresh(ui);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    Header(ui);
    if (ImGui::BeginTabBar("pages")) {
        const char* names[] = {"Game", "Graphics", "Controls", "Online", "Saves", "Help"};
        void (*pages[])(Ui&) = {PageGame, PageGraphics, PageControls, PageOnline, PageSaves, PageHelp};
        for (int i = 0; i < 6; ++i) {
            const ImGuiTabItemFlags f = ui.force_tab == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(names[i], nullptr, f)) {
                ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
                pages[i](ui);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    Modals(ui);
    ImGui::End();
    // Settings are kept as they change.
    if (!ui.shot_mode) {
        const std::string ini = ToIni(ui.s);
        if (ini != ui.saved_ini && SaveSettings(ui.s)) ui.saved_ini = ini;
    }
}

bool SaveShot(SDL_Renderer* r, const std::string& path) {
    SDL_Surface* surf = SDL_RenderReadPixels(r, nullptr);
    if (!surf) return false;
    const bool ok = SDL_SaveBMP(surf, path.c_str());
    SDL_DestroySurface(surf);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    std::string shot_dir, print_file;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0) shot_dir = argv[i + 1];
        if (std::strcmp(argv[i], "--print-launch") == 0) print_file = argv[i + 1];
    }
    // --print-launch FILE: what Play would start with the saved settings (no window, nothing started,
    // the Quit Game setup not run, only named).
    if (!print_file.empty()) {
        const Settings s = LoadSettings();
        const Paths p = ResolvePaths(s);
        std::ofstream o(print_file);
        o << "exe=" << p.exe.string() << "\ncwd=" << p.root.string() << "\nlog=analysis\\" << p.log_base << "_log.txt\nargs=";
        for (const auto& a : MapSettings(s).args) o << a << " ";
        o << "\n";
        for (const auto& [k, v] : GameEnvironment(s, p, {})) o << k << "=" << v << "\n";
        if (s.quit_menu)
            o << "# Quit Game: python " << (p.worktree / "tools" / "quit_menu_setup.py").string()
              << " would run first, then LBP3_FILE_EDITS=" << (p.worktree / "user" / "file_edits.bin").string()
              << " and LBP3_QUIT_MENU=1 are added\n";
        for (const auto& c : RunChecks(s, p)) o << (c.ok ? "# ok   " : "# FAIL ") << c.what << ": " << c.detail << "\n";
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        MessageBoxA(nullptr, SDL_GetError(), "LBP3 Launcher", MB_ICONERROR);
        return 1;
    }
    const float scale = [] {
        const float s = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        return s > 0.f ? s : 1.f;
    }();
    // --shot: on a secondary display if there is one, never taking the focus (the player may be
    // playing on the main one).
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (!shot_dir.empty()) {
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
        flags |= SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_HIDDEN;
    }
    SDL_Window* window = SDL_CreateWindow("LBP3 Launcher", static_cast<int>(1000 * scale), static_cast<int>(720 * scale), flags);
    if (window && !shot_dir.empty()) {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
        for (int i = 0; displays && i < count; ++i) {
            if (displays[i] == primary) continue;
            SDL_Rect r;
            if (SDL_GetDisplayBounds(displays[i], &r)) SDL_SetWindowPosition(window, r.x + 40, r.y + 40);
            break;
        }
        SDL_free(displays);
        SDL_ShowWindow(window);
    }
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        MessageBoxA(nullptr, SDL_GetError(), "LBP3 Launcher", MB_ICONERROR);
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowMinimumSize(window, static_cast<int>(760 * scale), static_cast<int>(560 * scale));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the launcher keeps its own settings
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const char* windir = std::getenv("WINDIR");
    const std::string fonts = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
    std::error_code ec;
    if (fs::exists(fonts + "segoeui.ttf", ec)) g_font = io.Fonts->AddFontFromFileTTF((fonts + "segoeui.ttf").c_str(), 0.f);
    if (fs::exists(fonts + "seguisb.ttf", ec)) g_bold = io.Fonts->AddFontFromFileTTF((fonts + "seguisb.ttf").c_str(), 0.f);
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    if (!g_bold) g_bold = g_font;
    io.FontDefault = g_font;
    ApplyStyle(scale);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Ui ui;
    ui.s = LoadSettings();
    ui.saved_ini = ToIni(ui.s);
    ui.shot_mode = !shot_dir.empty();
    const char* shot_names[] = {"game", "graphics", "controls", "online", "saves", "help"};
    int shot_page = 0, shot_frames = 0;
    if (ui.shot_mode) ui.force_tab = 0;

    bool running = true, minimized_for_game = false;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)) running = false;
        }
        // Minimized while the game runs (if wanted), back when it ends.
        GameState state;
        {
            std::lock_guard lock(g_run.m);
            state = g_run.state;
        }
        if (state == GameState::Running && ui.s.minimize_while_playing && !minimized_for_game) {
            SDL_MinimizeWindow(window);
            minimized_for_game = true;
        } else if (state != GameState::Running && state != GameState::Starting && minimized_for_game) {
            SDL_RestoreWindow(window);
            SDL_RaiseWindow(window);
            minimized_for_game = false;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(100);
            continue;
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        Frame(ui);
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 22, 34, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (ui.shot_mode && ++shot_frames == 4) {
            SaveShot(renderer, shot_dir + "\\launcher_" + shot_names[shot_page] + ".bmp");
            shot_frames = 0;
            if (++shot_page == 6) running = false;
            else ui.force_tab = shot_page;
        } else if (ui.shot_mode && shot_frames == 2) {
            ui.force_tab = -1;  // SetSelected only needs one frame
        }
        SDL_RenderPresent(renderer);
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    CoUninitialize();
    return 0;
}
