// LBP3Launcher's pages (launcher_app.h): Home, Game, Graphics & sound, Controls (and the controls
// editor), DLC, Saves, Controllers, What's new, Tools (updates), Help, the setup and the tour.
#include "launcher_app.h"

#include <imgui_internal.h>

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

#include "launcher_proc.h"
#include "launcher_shell.h"
#include "launcher_theme.h"
#include "input/action_table.h"

namespace launcher::ui {

static fs::path PkgExtractor(const App& a) {
    if (!a.s.pkg_extractor.empty()) return fs::path(a.s.pkg_extractor);
    return a.p.game_dir / "pkg_extractor.exe";
}

static void StartDlcInstall(App& a, std::vector<std::string> pkgs) {
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
        a.dlc_dirty = true;
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

static void StartUpdateBuild(App& a) {
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

void PageDlc(App& a) {
    if (a.dlc_dirty && !a.dlc_install.running) {
        a.dlc = ListDlc(a.p.dlc_dir);
        a.dlc_dirty = false;
    }
    Heading("Installed DLC");
    Dim("%s", a.p.dlc_dir.string().c_str());
    ImGui::Text("%d packs", static_cast<int>(a.dlc.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Open the DLC folder")) OpenPath(a.p.dlc_dir);
    ImGui::SameLine();
    ImGui::BeginDisabled(a.shot_mode || GameBusy(a));
    if (ImGui::SmallButton("Use another folder...")) {
        const std::string picked = PickFolder(L"Choose your DLC folder");
        if (!picked.empty()) a.s.dlc_dir = picked, Refresh(a, true), a.dlc_dirty = true;
    }
    if (!a.s.dlc_dir.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Back to the default")) a.s.dlc_dir.clear(), Refresh(a, true), a.dlc_dirty = true;
    }
    ImGui::EndDisabled();
    Help("The game looks for DLC in <dump>\\CUSA00473-DLC; another folder is passed to it as LBP3_ADDCONT_DIR.");
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
    Dim("Packs whose own title is a placeholder (\"1\", or one shared name, as most license-only packs have) are listed by their label, dimmed.");
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
            if (d.placeholder) Dim("%s", d.title.c_str());
            else ImGui::TextUnformatted(d.title.c_str());
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
        if (name.empty()) ToastMsg(a, "Backup failed: " + error, kBad);
        else ToastMsg(a, "Saves backed up as " + name, kGood), AppendLauncherLog(a.p, "saves backed up as " + name);
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

static void Chip(const char* label, bool on) {
    ImGui::PushStyleColor(ImGuiCol_Button, on ? kCardboard : ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? kCardboard : ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, on ? kCardboard : ImVec4(30 / 255.f, 38 / 255.f, 58 / 255.f, 1.f));
    ImGui::Button(label, ImVec2(S(84), 0));
    ImGui::PopStyleColor(3);
}

static void Stick(const char* id, float x, float y) {
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
        ImGui::TextColored(kCardboard, "Filtered (what the game gets with the motion filter on)");
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
    if (a.portable) {  // a package: its release notes (RELEASE-NOTES.md beside the launcher)
        Heading(a.version.empty() ? "This release" : ("This release (" + a.version + ")").c_str());
        if (a.highlights.empty()) Dim("(RELEASE-NOTES.md isn't next to the launcher)");
        for (const auto& h : a.highlights) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", h.c_str());
        }
        Dim("Full notes, known issues and credits: RELEASE-NOTES.md, README-FRIENDS.md and CREDITS.md in this folder.");
        return;
    }
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

static void JobLog(Job& j, const char* id, float h) {
    std::lock_guard lock(j.m);
    if (!j.running && !j.done) return;
    if (j.done) ImGui::TextColored(j.ok ? kGood : kWarn, "%s", j.message.c_str());
    ImGui::BeginChild(id, ImVec2(0, S(h)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& l : j.lines) ImGui::TextUnformatted(l.c_str());
    if (j.running) ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
}

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

// ---------------------------------------------------------------------------------------------
// Home

namespace {

// The render size dropdown (Resolutions(), LBP3_RES).
void ResolutionCombo(const char* id, std::string& res, float width) {
    const auto& list = Resolutions();
    const std::string cur = CanonicalRes(res);
    const char* shown = cur.empty() ? res.c_str() : cur.c_str();
    for (const Resolution& r : list)
        if (cur == r.value) shown = r.name;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, shown)) {
        for (const Resolution& r : list)
            if (ImGui::Selectable(r.name, cur == r.value)) res = r.value;
        ImGui::EndCombo();
    }
}

void StatCard(const char* id, Icon icon, const char* title, const std::string& value, const std::string& sub, float w) {
    if (BeginCard(id, ImVec2(w, S(118)))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(p.x + S(16), p.y + S(16)), S(17), Col(kCardboard, 0.18f));
        DrawIcon(dl, icon, ImVec2(p.x + S(16), p.y + S(16)), S(18), Col(kCardboard));
        ImGui::SetCursorScreenPos(ImVec2(p.x + S(42), p.y + S(4)));
        ImGui::TextColored(kDim, "%s", title);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + S(38)));
        ImGui::PushFont(g_heading, 26.f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopFont();
        if (!sub.empty()) Dim("%s", sub.c_str());
    }
    EndCard();
}

}  // namespace

void PageHome(App& a) {
    PlayStats st;
    {
        std::lock_guard lock(a.stats_m);
        st = a.stats;
    }
    const float w = ImGui::GetContentRegionAvail().x;
    const int hour = [] {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        return tm.tm_hour;
    }();
    const Settings eff = Eff(a);
    const std::string who = ValidOnlineName(eff.online_name) ? ", " + eff.online_name : std::string();
    const std::string hello = (hour < 5 ? "Up late" : hour < 12 ? "Good morning" : hour < 18 ? "Good afternoon" : "Good evening") + who;
    PageTitle(hello.c_str(), st.last_level.empty() ? "Ready when you are." : ("Last time you were in " + st.last_level + ".").c_str());

    const float cw = (w - S(36)) / 4;
    StatCard("c1", Icon::Clock, "Played in total", st.sessions ? Duration(st.seconds) : std::string("-"),
             st.sessions ? std::to_string(st.sessions) + " sessions" : std::string("nothing yet"), cw);
    ImGui::SameLine();
    StatCard("c2", Icon::Play, "Last played", st.last_played ? When(st.last_played) : std::string("-"), "", cw);
    ImGui::SameLine();
    StatCard("c3", Icon::Level, "Last level", st.last_level.empty() ? std::string("-") : st.last_level, "", cw);
    ImGui::SameLine();
    StatCard("c4", Icon::Dlc, "DLC packs", std::to_string(a.dlc.size()), "installed", cw);

    ImGui::Dummy(ImVec2(0, S(6)));
    const float half = (w - S(12)) * 0.5f;
    if (BeginCard("quick", ImVec2(half, S(236)))) {
        Heading("Quick settings");
        ImGui::TextUnformatted("Resolution");
        ImGui::SameLine(S(170));
        ResolutionCombo("##quickres", a.s.res, S(220));
        Toggle("60 frames a second on screen", &a.s.framegen, "Frame generation: a generated frame between each pair of real ones.");
        Toggle("Play online", &a.s.online, "Signs in to the community server with your active account (Online page).");
        Toggle("Skip the logos", &a.s.skip_intro, nullptr);
        if (!EssentialsOk(a) && BigButton("Run the setup", ImVec2(S(180), 0), kOrange)) a.wizard = true, a.wizard_step = 0;
    }
    EndCard();
    ImGui::SameLine();
    if (BeginCard("whatsnew", ImVec2(half, S(236)))) {
        Heading(a.portable && !a.version.empty() ? ("What's new in " + a.version).c_str() : "What's new");
        std::lock_guard lock(a.news_m);
        if (a.portable) {  // a package: the release notes' highlights, one line each
            const float room = ImGui::GetContentRegionAvail().x - S(24);
            if (a.highlights.empty()) Dim("(RELEASE-NOTES.md isn't next to the launcher)");
            for (size_t i = 0; i < a.highlights.size() && i < 5; ++i) {
                std::string line = a.highlights[i];
                if (const size_t cut = line.find(" ("); cut != std::string::npos && cut > 20) line.resize(cut);
                while (line.size() > 4 && ImGui::CalcTextSize(line.c_str()).x > room) line.resize(line.size() - 4), line += "...";
                ImGui::Bullet();
                ImGui::TextUnformatted(line.c_str());
            }
            if (!a.highlights.empty() && ImGui::SmallButton("More...")) a.page = News;
        } else if (a.news.empty()) {
            Dim("(this build's history appears here)");
        }
        // one line each: the subject up to its first ':' or ';' clause, shortened to fit
        const float room = ImGui::GetContentRegionAvail().x - S(100);
        for (size_t i = 0; !a.portable && i < a.news.size() && i < 5; ++i) {
            ImGui::TextColored(kCardboard, "%s", a.news[i].when.c_str());
            ImGui::SameLine(S(108));
            std::string line = a.news[i].subject;
            if (const size_t cut = line.find(" ("); cut != std::string::npos && cut > 20) line.resize(cut);
            while (line.size() > 4 && ImGui::CalcTextSize(line.c_str()).x > room) line.resize(line.size() - 4), line += "...";
            ImGui::TextUnformatted(line.c_str());
        }
        if (!a.portable && !a.news.empty() && ImGui::SmallButton("More...")) a.page = News;
    }
    EndCard();

    if (BeginCard("tips", ImVec2(w, 0), false)) {
        Heading("Good to know");
        ImGui::BulletText("Insert, or L3+R3 together, opens the port's own menu in the game (frame rate, volume, Quit).");
        ImGui::BulletText("F11 switches the game between a window and full screen.");
        ImGui::BulletText("The Community page shows the server's levels; \"Queue\" puts one in your in-game queue.");
    }
    EndCard();
}

// ---------------------------------------------------------------------------------------------
// Game

void PageGame(App& a) {
    Settings& s = a.s;
    PageTitle("Game", "Which build, how it starts, language and menus.");
    if (!a.portable) {
        if (BeginCard("build", ImVec2(0, 0), false)) {
            Heading("Which game build");
            const char* builds[] = {"This launcher's build (next to LBP3Launcher.exe)", "Best of both worlds (lbp3-wt\\best)",
                                    "Port (the game folder's own build, as play.bat)", "Another lbp3.exe..."};
            int b = static_cast<int>(s.build);
            ImGui::SetNextItemWidth(S(520));
            if (ImGui::Combo("##build", &b, builds, IM_ARRAYSIZE(builds))) s.build = static_cast<Build>(b), Refresh(a, true), a.news_loaded = false;
            Help("Which lbp3.exe to start. \"Best of both worlds\" and \"Port\" are this PC's development builds.");
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
        }
        EndCard();
    }
    if (BeginCard("start", ImVec2(0, 0), false)) {
        Heading("Starting");
        Toggle("Skip the logos and the intro movie", &s.skip_intro, "The PlayStation and studio logos and the live-action intro.");
        Toggle("Minimize the launcher while playing", &s.minimize_while_playing, "It comes back when the game closes.");
        Toggle("Hide in the notification area while playing", &s.tray_while_playing,
               "The launcher goes to the notification area (by the clock) while the game runs, and comes back when it closes.");
        Toggle("Ask before closing the launcher while the game runs", &s.confirm_quit, nullptr);
    }
    EndCard();
    if (BeginCard("lang", ImVec2(0, 0), false)) {
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
        Help("The US edition normally shows English or French only; any other choice also unlocks the languages it hides.");
    }
    EndCard();
    if (BeginCard("menus", ImVec2(0, 0), false)) {
        Heading("Menus");
        Toggle("In-game menu (Insert, or L3+R3 together)", &s.menu, "Frame rate and latency, frame generation on/off, volume, a small stats corner, and Quit.");
        ImGui::BeginDisabled(!s.menu);
        Toggle("Show the stats corner from the start", &s.stats, "The in-game menu can turn it on and off too.");
        ImGui::EndDisabled();
        Toggle("\"Quit Game\" in the game's pause menu", &s.quit_menu,
               "Replaces the Good Grief! button, which is greyed out offline; press it twice. The launcher writes the edit "
               "itself before Play (a port of LBPPC's quit_menu.py); the game files are never changed.");
    }
    EndCard();
    if (BeginCard("unlocks", ImVec2(0, 0), false)) {
        Heading("LBP1 and LBP2 items");
        Toggle("Import the LBP1/LBP2 items once (the old \"My Unlocks\")", &s.my_unlocks, "Runs the game's own import the first time you open the start menu (Options).");
        if (s.my_unlocks) {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
            ImGui::TextWrapped("On the first Options press the import screen appears: wait about 2 minutes, then press Cross on "
                               "\"successfully imported\". Then close the game and switch this off again. Back up your saves first.");
            ImGui::PopStyleColor();
        }
    }
    EndCard();
}

// ---------------------------------------------------------------------------------------------
// Graphics and sound

void PageGraphics(App& a) {
    Settings& s = a.s;
    PageTitle("Graphics & sound", "Resolution, frame generation, the display and window, volume.");
    if (BeginCard("res", ImVec2(0, 0), false)) {
        Heading("Picture");
        ImGui::TextUnformatted("Resolution");
        ImGui::SameLine(S(120));
        ResolutionCombo("##res", s.res, S(320));
        Help("The size the game draws at. Above 1080p it's sharper and costs more GPU time; a size wider than "
             "16:9 widens the view, and shows with black bars on a 16:9 screen. Takes effect when the game starts.");
        Toggle("60 frames a second on screen (the game itself stays at 30)", &s.framegen, "A generated frame between each pair of real ones. About 17 ms more input lag.");
        ImGui::BeginDisabled(!s.framegen);
        Toggle("Keep in-world text readable", &s.fg_text, "Generated frames keep thin bright text sharp (the map's \"Loading...\" ring).");
        ImGui::EndDisabled();
    }
    EndCard();
    if (BeginCard("display", ImVec2(0, 0), false)) {
        Heading("Display and window");
        // the displays SDL sees, by number (LBP3_DISPLAY), plus "by refresh rate" for setups whose numbering changes
        int count = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&count);
        std::vector<std::pair<std::string, std::string>> choices{{"", "Where Windows puts it"}};
        for (int i = 0; ids && i < count; ++i) {
            const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(ids[i]);
            char label[200];
            std::snprintf(label, sizeof label, "%d: %s (%dx%d, %.0f Hz)", i + 1, SDL_GetDisplayName(ids[i]), m ? m->w : 0, m ? m->h : 0,
                          m ? m->refresh_rate : 0.f);
            choices.emplace_back(std::to_string(i + 1), label);
            if (m && m->refresh_rate > 0) {
                char hz[32];
                std::snprintf(hz, sizeof hz, "%.0fhz", m->refresh_rate);
                char l2[220];
                std::snprintf(l2, sizeof l2, "The %.0f Hz display (follows it if the numbering changes)", m->refresh_rate);
                if (std::none_of(choices.begin(), choices.end(), [&](const auto& c) { return c.first == hz; })) choices.emplace_back(hz, l2);
            }
        }
        SDL_free(ids);
        std::string current = "Where Windows puts it";
        for (const auto& [k, l] : choices)
            if (k == s.display) current = l;
        if (!s.display.empty() && current == "Where Windows puts it") current = s.display + " (not connected now)";
        ImGui::SetNextItemWidth(S(520));
        if (ImGui::BeginCombo("Display", current.c_str())) {
            for (const auto& [k, l] : choices)
                if (ImGui::Selectable(l.c_str(), k == s.display)) s.display = k;
            ImGui::EndCombo();
        }
        Help("Which monitor the game opens on (LBP3_DISPLAY).");
        int mode = s.window_mode == "borderless" ? 1 : s.window_mode == "fullscreen" ? 2 : 0;
        ImGui::TextUnformatted("Window");
        ImGui::SameLine(S(120));
        if (ImGui::RadioButton("Windowed", &mode, 0)) s.window_mode = "windowed";
        ImGui::SameLine();
        if (ImGui::RadioButton("Borderless (the whole display)", &mode, 1)) s.window_mode = "borderless";
        ImGui::SameLine();
        if (ImGui::RadioButton("Full screen", &mode, 2)) s.window_mode = "fullscreen";
        Help("Borderless covers the display without changing its mode (switching windows stays quick). Full screen takes "
             "the display's own mode. F11 in the game switches between the window and full screen at any time.");
    }
    EndCard();
    if (BeginCard("sound", ImVec2(0, 0), false)) {
        Heading("Sound");
        ImGui::SetNextItemWidth(S(320));
        ImGui::SliderInt("Volume at start", &s.volume, 0, 100, "%d%%");
        Help("The game's master volume when it starts (LBP3_VOLUME). The in-game menu (Insert) changes it while playing.");
        Toggle("Mix surround down to stereo in the port", &s.downmix,
               "The game plays 7.1; on stereo speakers or headphones the port mixes it down itself (clearer dialogue). Off = "
               "SDL's own mix-down (LBP3_DOWNMIX=sdl).");
    }
    EndCard();
    if (BeginCard("shaders", ImVec2(0, 0), false)) {
        Heading("Shaders and startup");
        const char* caches[] = {"Automatic (its own copy for builds other than the game folder's)", "This build's own copy (user\\shader_cache_best)",
                                "The normal one (user\\shader_cache)"};
        int c = static_cast<int>(s.cache);
        ImGui::SetNextItemWidth(S(520));
        if (ImGui::Combo("Shader cache", &c, caches, IM_ARRAYSIZE(caches))) s.cache = static_cast<CacheChoice>(c), Refresh(a, true);
        Help("Where compiled shaders are kept.");
        ImGui::SetNextItemWidth(S(200));
        ImGui::SliderInt("Startup threads (0 = all cores but one)", &s.pipeline_threads, 0, 32);
        Toggle("Save the graphics driver's own shader cache", &s.vk_pipeline_cache, "Later startups rebuild faster.");
        Toggle("Build new shaders in the background", &s.async_shaders,
               "No freeze the first time something new is drawn; instead it can appear a moment late. Takes "
               "effect when the game starts.");
        int flips = s.status_flips == 300 ? 1 : 0;
        if (ImGui::RadioButton("Status lines every second", &flips, 0)) s.status_flips = 30;
        ImGui::SameLine();
        if (ImGui::RadioButton("Every 10 seconds (smaller logs)", &flips, 1)) s.status_flips = 300;
    }
    EndCard();
}

// ---------------------------------------------------------------------------------------------
// Controls (and the keyboard / pad editor)

void PageControls(App& a) {
    Settings& s = a.s;
    ControlsState& c = a.controls;
    PageTitle("Controls", "Motion, the touch pad, and your keys and pad buttons.");
    if (BeginCard("motion", ImVec2(0, 0), false)) {
        Heading("Motion and touch");
        Toggle("Filtered motion controls", &s.motion_filter, "The gyro's slow drift is learned away while the pad is still.");
        Toggle("Tilt with the number pad (4/6 and 8/2)", &s.tilt_keys, "For pads without motion sensors.");
        Toggle("The mouse as the touch pad", &s.mouse_touch, "F7 captures and releases the mouse; click = touch; the wheel pinches.");
    }
    EndCard();
    const fs::path ini = a.p.root / "user" / "config" / "input.ini";
    if (!c.loaded) {
        c.config = InputConfig::Load(ini);
        c.loaded = true;
    }
    // a key or button arrived from the event loop
    if (!c.capture_key.empty() && !c.captured.empty()) {
        if (c.captured != "\x1b") {
            if (c.capture_add) {
                std::vector<std::string> keys = SplitKeys(c.config.Get(c.capture_key));
                if (std::find(keys.begin(), keys.end(), c.captured) == keys.end()) keys.push_back(c.captured);
                std::string v;
                for (const auto& k : keys) v += (v.empty() ? "" : ", ") + k;
                c.config.Set(c.capture_key, v);
            } else {
                c.config.Set(c.capture_key, c.captured);
            }
        }
        c.capture_key.clear();
        c.captured.clear();
    }
    if (BeginCard("keys", ImVec2(0, 0), false)) {
        Heading("Keyboard");
        Dim("Press Change, then the key. The game reads these when it starts (user\\config\\input.ini; your old file is kept as input.ini.bak).");
        const auto clashes = c.config.Clashes();
        if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, S(260));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(250));
            for (const auto& row : ControlRows()) {
                if (row.pad) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.label.c_str());
                ImGui::TableNextColumn();
                const bool waiting = c.capture_key == row.key;
                const std::string keys = c.config.Get(row.key);
                bool clash = false;
                for (const auto& k : SplitKeys(keys)) clash |= clashes.count(k) > 0;
                if (waiting) ImGui::TextColored(kOrange, "press a key...");
                else ImGui::TextColored(clash ? kWarn : kCardboard, "%s", keys.empty() ? "(none)" : keys.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(row.key.c_str());
                ImGui::BeginDisabled(a.shot_mode);
                if (waiting) {
                    if (ImGui::SmallButton("Cancel")) c.capture_key.clear();
                } else {
                    if (ImGui::SmallButton("Change")) c.capture_key = row.key, c.capture_add = false, c.captured.clear();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Add a key")) c.capture_key = row.key, c.capture_add = true, c.captured.clear();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Default")) c.config.Reset(row.key);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!clashes.empty()) {
            std::string list;
            for (const auto& [k, v] : clashes) list += (list.empty() ? "" : ", ") + k;
            ImGui::TextColored(kWarn, "Used by more than one action: %s", list.c_str());
        }
    }
    EndCard();
    if (BeginCard("pad", ImVec2(0, 0), false)) {
        Heading("Pad buttons");
        Dim("Which of your pad's buttons gives each PS4 button. Untouched buttons stay as they are (positional: the bottom face "
            "button is Cross on any pad).");
        if (!a.pads_allowed) ImGui::TextColored(kWarn, "The game is running: \"Press\" works once it is closed.");
        const auto choices = PadButtonChoices();
        if (ImGui::BeginTable("pad", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("PS4 button", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("From your pad's", ImGuiTableColumnFlags_WidthFixed, S(330));
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(180));
            for (const auto& row : ControlRows()) {
                if (!row.pad) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.label.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(row.key.c_str());
                const std::string cur = c.config.Get(row.key);
                const bool waiting = c.capture_key == row.key;
                ImGui::SetNextItemWidth(S(320));
                if (waiting) ImGui::TextColored(kOrange, "press a button on the pad (Esc cancels)...");
                else if (ImGui::BeginCombo("##b", cur.c_str())) {
                    for (const auto& [name, label] : choices)
                        if (ImGui::Selectable((name + "  (" + label + ")").c_str(), name == cur)) c.config.Set(row.key, name);
                    ImGui::EndCombo();
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!a.pads_allowed || a.shot_mode);
                if (!waiting && ImGui::SmallButton("Press")) {
                    c.capture_key = row.key, c.capture_add = false, c.captured.clear();
                    SyncPads(a);  // the pad's buttons arrive as events once it is open
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Default")) c.config.Reset(row.key);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    EndCard();
    ImGui::BeginDisabled(!c.config.Changed() || a.shot_mode);
    if (BigButton("Save controls", ImVec2(S(200), 0), kPlay)) {
        std::string error;
        if (c.config.Save(ini, &error)) ToastMsg(a, "Controls saved; the game uses them next time it starts.", kGood);
        else ToastMsg(a, "Couldn't save the controls: " + error, kBad);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (BigButton("Undo changes", ImVec2(S(170), 0), kCard, c.config.Changed())) c.loaded = false;
    ImGui::SameLine();
    if (BigButton("Everything to defaults", ImVec2(S(240), 0), kCard)) c.config.ResetAll();
}

// ---------------------------------------------------------------------------------------------
// Tools: the file check, updates (releases or git), shortcuts, folders and logs

bool DevCheckout(const App& a) {
    std::error_code ec;
    return !a.portable && !a.p.worktree.empty() && fs::exists(a.p.worktree / ".git", ec);
}

void StartReleaseCheck(App& a) {
    if (a.release_check.running || a.release_install.running) return;
    a.release_check.Start();
    std::thread([&a] {
        ReleaseInfo r = FetchLatestRelease();
        const std::string mine = InstalledVersion(LauncherDir());
        std::string msg;
        bool ok = r.ok;
        if (!r.ok) msg = r.error;
        else if (mine.empty()) msg = r.tag + " is the latest release (this is a development build)";
        else if (CompareVersions(ParseVersion(r.tag), ParseVersion(mine)) > 0) msg = r.tag + " is out (you have " + mine + ")";
        else msg = "Up to date (" + mine + ")";
        {
            std::lock_guard lock(a.release_m);
            a.release = std::move(r);
        }
        a.release_check.Finish(ok, msg);
    }).detach();
}

namespace {

void StartReleaseInstall(App& a) {
    if (a.release_install.running || a.release_check.running) return;
    ReleaseInfo r;
    {
        std::lock_guard lock(a.release_m);
        r = a.release;
    }
    if (!r.ok) return;
    a.release_install.Start();
    std::thread([&a, r] {
        Job& j = a.release_install;
        auto fail = [&](const std::string& e) {
            j.Line("! " + e);
            j.Finish(false, e);
        };
        if (GameRunning()) return fail("close the game first");
        wchar_t tmpw[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpw);
        const fs::path tmp = fs::path(tmpw) / ("lbp3_update_" + r.tag);
        std::error_code ec;
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        const fs::path zip = tmp / r.zip_name;
        j.Line("Downloading " + r.zip_name + "...");
        std::string error;
        if (!DownloadFile(r.zip_url, zip, [&](std::uint64_t d, std::uint64_t t) {
                std::lock_guard lock(j.m);
                j.progress = t ? static_cast<float>(double(d) / double(t)) : -1.f;
            }, &j.cancel, &error))
            return fail("download: " + error);
        // Every release attaches LBP3-Recompiled-<tag>.zip.sha256; one without it isn't installed.
        if (r.sha_url.empty()) return fail("the release has no .sha256 to check the download against; nothing was changed");
        {
            j.Line("Checking it against its .sha256...");
            const HttpReply sha = HttpJson("GET", r.sha_url, "", "");
            const std::string want = ParseSha256Text(sha.body);
            // GitHub serves the .sha256 through a redirect HttpJson may not follow: then download it too
            std::string want2 = want;
            if (want2.empty() && DownloadFile(r.sha_url, tmp / "zip.sha256", nullptr, &j.cancel, &error)) want2 = ParseSha256Text(ReadFile(tmp / "zip.sha256"));
            if (want2.empty()) return fail("couldn't read the release's .sha256");
            if (Sha256File(zip) != want2) return fail("the download doesn't match its .sha256 (damaged); nothing was changed");
        }
        j.Line("Unpacking...");
        if (!ExtractZip(zip, tmp / "unpacked", &error)) return fail(error);
        if (GameRunning()) return fail("close the game first");
        j.Line("Installing (user\\ and analysis\\ are left alone)...");
        std::vector<std::string> changed;
        if (!ApplyUpdate(tmp / "unpacked", LauncherDir(), &changed, &error)) return fail(error);
        AppendLauncherLog(a.p, "updated to " + r.tag + " (" + std::to_string(changed.size()) + " files)");
        j.Line(std::to_string(changed.size()) + " files updated. Restarting...");
        j.Finish(true, "Updated to " + r.tag);
        // the new launcher, then this one goes
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" --updated " + std::wstring(r.tag.begin(), r.tag.end());
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, LauncherDir().c_str(), &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            a.close_now = true;
        }
    }).detach();
}

}  // namespace

void PageTools(App& a) {
    PageTitle("Tools", "Checks, updates, shortcuts and folders.");
    if (BeginCard("check", ImVec2(0, 0), false)) {
        Heading("Check the game files");
        Dim("Checks the dump: eboot.bin, param.sfo (LittleBigPlanet 3 US, 01.28), the system files, the five big archives "
            "(their tables, and a few files inside against their hashes) and a sample of the level archives.");
        ImGui::BeginDisabled(a.file_check.running || a.shot_mode);
        if (ImGui::Button("Check game files")) StartFileCheck(a);
        ImGui::EndDisabled();
        if (a.file_check.running) {
            ImGui::SameLine();
            Spinner(S(8), kCardboard);
            ImGui::SameLine();
            std::lock_guard lock(a.file_check.m);
            Dim("%s", a.file_check.lines.empty() ? "" : a.file_check.lines.back().c_str());
        }
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
    EndCard();

    if (BeginCard("updates", ImVec2(0, 0), false)) {
        Heading("Updates");
        Toggle("Check for updates when the launcher opens", &a.s.update_on_start,
               DevCheckout(a) ? "Looks for new commits on this build's branch (git fetch)." : "Asks GitHub for the latest release.");
        if (!DevCheckout(a)) {
            const std::string& mine = a.version;
            ImGui::Text("Installed: %s", mine.empty() ? "a development build" : mine.c_str());
            Dim("Updates come from the releases of %s on GitHub. Your saves, settings, accounts and logs (user\\, analysis\\) are "
                "never touched; the old program files are kept as *.old until the next start.", kReleaseRepo);
            ImGui::BeginDisabled(a.release_check.running || a.release_install.running || a.shot_mode);
            if (ImGui::Button("Check for updates")) StartReleaseCheck(a);
            ImGui::EndDisabled();
            {
                std::lock_guard lock(a.release_check.m);
                if (a.release_check.running) ImGui::SameLine(), Spinner(S(8), kCardboard);
                else if (a.release_check.done) ImGui::SameLine(), ImGui::TextColored(a.release_check.ok ? kGood : kWarn, "%s", a.release_check.message.c_str());
            }
            ReleaseInfo r;
            {
                std::lock_guard lock(a.release_m);
                r = a.release;
            }
            const std::string& mineV = a.version;
            const bool newer = r.ok && (mineV.empty() || CompareVersions(ParseVersion(r.tag), ParseVersion(mineV)) > 0);
            if (r.ok) {
                ImGui::TextColored(kCardboard, "%s", (r.name.empty() ? r.tag : r.name).c_str());
                if (!r.published.empty()) ImGui::SameLine(), Dim("%s", r.published.c_str());
                if (!r.notes.empty()) {
                    ImGui::BeginChild("notes", ImVec2(0, S(110)), ImGuiChildFlags_Borders);
                    ImGui::TextWrapped("%s", r.notes.c_str());
                    ImGui::EndChild();
                }
                if (newer) {
                    // not while a check runs: the install would take the release the previous check found
                    const bool can = !GameBusy(a) && !a.release_install.running && !a.release_check.running;
                    if (BigButton(("Download and install " + r.tag).c_str(), ImVec2(S(340), 0), kPlay, can)) StartReleaseInstall(a);
                    if (GameBusy(a)) ImGui::SameLine(), ImGui::TextColored(kWarn, "close the game first");
                }
            }
            std::lock_guard lock(a.release_install.m);
            if (a.release_install.running && a.release_install.progress >= 0) ImGui::ProgressBar(a.release_install.progress, ImVec2(S(420), 0));
            if (a.release_install.running || a.release_install.done) {
                if (a.release_install.done) ImGui::TextColored(a.release_install.ok ? kGood : kBad, "%s", a.release_install.message.c_str());
                for (const auto& l : a.release_install.lines) Dim("%s", l.c_str());
            }
        } else {
            Dim("This is a development checkout: updates come from git (the \"%s\" remote) and are built here. Your saves are never touched.",
                a.s.update_remote.c_str());
            Dim("Build folder: %s", a.p.worktree.string().c_str());
            ImGui::BeginDisabled(a.update_check.running || a.update_build.running || a.shot_mode);
            if (ImGui::Button("Check for updates")) StartUpdateCheck(a);
            ImGui::SameLine();
            const bool can_build = !GameBusy(a);
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
        }
    }
    EndCard();

    if (BeginCard("shortcuts", ImVec2(0, 0), false)) {
        Heading("Shortcuts");
        ImGui::BeginDisabled(a.shot_mode);
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        if (ImGui::Button("Create a desktop shortcut")) {
            std::string error;
            const fs::path made = CreateShortcut(ShortcutPlace::Desktop, self, &error);
            ToastMsg(a, made.empty() ? "Shortcut failed: " + error : "Shortcut made on the desktop", made.empty() ? kBad : kGood);
        }
        ImGui::SameLine();
        if (ImGui::Button("Add to the Start menu")) {
            std::string error;
            const fs::path made = CreateShortcut(ShortcutPlace::StartMenu, self, &error);
            ToastMsg(a, made.empty() ? "Shortcut failed: " + error : "Added to the Start menu", made.empty() ? kBad : kGood);
        }
        ImGui::EndDisabled();
    }
    EndCard();

    if (BeginCard("folders", ImVec2(0, 0), false)) {
        Heading("Folders and logs");
        if (ImGui::Button("Game folder")) OpenPath(a.p.root);
        ImGui::SameLine();
        if (ImGui::Button("Saves")) OpenPath(a.p.saves);
        ImGui::SameLine();
        if (ImGui::Button("DLC folder")) OpenPath(a.p.dlc_dir);
        ImGui::SameLine();
        if (ImGui::Button("Screenshots")) {
            // Windows' own captures folder (Win+Alt+PrtScn, Game Bar); the game's photos live in its saves.
            PWSTR videos = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &videos))) {
                const fs::path cap = fs::path(videos) / L"Captures";
                CoTaskMemFree(videos);
                std::error_code ec;
                fs::create_directories(cap, ec);
                OpenPath(cap);
            }
        }
        Help("Screenshots of the game window (Win+Alt+PrtScn, or the Game Bar) go to Windows' Videos\\Captures folder. Photos "
             "taken with the Popit stay inside the game's save data (Popit > Photos).");
        const fs::path latest = LatestLog(a.p);
        ImGui::BeginDisabled(latest.empty());
        if (ImGui::Button("Latest log")) OpenPath(latest);
        ImGui::SameLine();
        if (ImGui::Button("Copy the latest log")) {
            // the last 3000 lines (a whole log can be tens of megabytes), any account password masked
            std::string text = ReadFile(latest);
            size_t at = text.size(), lines = 0;
            while (at > 0 && lines < 3000) {
                at = text.rfind('\n', at - 1);
                if (at == std::string::npos) {
                    at = 0;
                    break;
                }
                ++lines;
            }
            text = text.substr(at);
            if (!a.acct.file.password.empty())
                for (size_t p = text.find(a.acct.file.password); p != std::string::npos; p = text.find(a.acct.file.password, p))
                    text.replace(p, a.acct.file.password.size(), "<password>");
            SDL_SetClipboardText(text.c_str());
            ToastMsg(a, "The latest log (" + std::to_string(lines) + " lines) is on the clipboard.", kGood);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("All logs")) OpenPath(a.p.root / "analysis" / "play_logs");
        ImGui::BeginDisabled(a.shot_mode);
        if (ImGui::Button("Make a report zip")) {
            std::string error;
            const fs::path zip = MakeReport(a.s, a.p, &error);
            if (zip.empty()) ToastMsg(a, "Report failed: " + error, kBad);
            else ToastMsg(a, "Report saved: " + zip.filename().string(), kGood), OpenPath(zip.parent_path());
        }
        ImGui::EndDisabled();
        Help("The latest logs, these settings and your PC's basic specs, for when something goes wrong. Your saves and online password are not included.");
    }
    EndCard();

    if (BeginCard("checks", ImVec2(0, 0), false)) {
        Heading("Checks");
        for (const auto& c : a.checks) {
            ImGui::TextColored(c.ok ? kGood : kWarn, c.ok ? "OK " : "!  ");
            ImGui::SameLine();
            ImGui::TextUnformatted(c.what.c_str());
            ImGui::SameLine();
            Dim("%s", c.detail.c_str());
        }
        if (!a.portable) {
            ImGui::TextUnformatted("Game folder");
            ImGui::SameLine();
            if (InputString("##root", a.s.game_root, "found automatically", 460)) Refresh(a, true);
            ImGui::SameLine();
            if (ImGui::Button("Browse...##root")) {
                const std::string picked = PickFolder(L"Choose the game folder");
                if (!picked.empty()) a.s.game_root = picked, Refresh(a, true);
            }
        }
    }
    EndCard();
}

// ---------------------------------------------------------------------------------------------
// Help

void PageHelp(App& a) {
    PageTitle("Help", "Getting around, and what to do when something goes wrong.");
    if (BeginCard("again", ImVec2(0, 0), false)) {
        Heading("Setup and tour");
        ImGui::BeginDisabled(a.shot_mode);
        if (ImGui::Button("Run the setup again")) a.wizard = true, a.wizard_step = 0;
        ImGui::SameLine();
        if (ImGui::Button("Show the tour")) a.tour = true, a.tour_step = 0;
        ImGui::EndDisabled();
    }
    EndCard();
    if (BeginCard("around", ImVec2(0, 0), false)) {
        Heading("Getting around");
        ImGui::BulletText("Mouse, or the keyboard: arrow keys and Enter; Ctrl+Tab and Ctrl+Shift+Tab change page.");
        ImGui::BulletText("Controller: the d-pad or left stick moves, Cross presses, Circle goes back; L1 and R1 change page.");
        ImGui::BulletText("While the game runs, the launcher lets go of your controllers completely.");
    }
    EndCard();
    if (BeginCard("ingame", ImVec2(0, 0), false)) {
        Heading("In the game");
        ImGui::BulletText("Insert, or L3+R3 together: the port's own menu (frame rate, frame generation, volume, Quit).");
        ImGui::BulletText("F11: window or full screen.   F7: the mouse as the touch pad (when that's on).");
        ImGui::BulletText("Your saves are in user\\savedata; the Saves page backs them up and restores them.");
    }
    EndCard();
    if (BeginCard("wrong", ImVec2(0, 0), false)) {
        Heading("When something goes wrong");
        ImGui::BulletText("The game doesn't start: Tools > Check game files, and the Checks list there.");
        ImGui::BulletText("It closes by itself: Tools > Copy the latest log, or Make a report zip for whoever helps you.");
        ImGui::BulletText("Online doesn't sign in: Online > Test sign-in says why in plain words.");
        ImGui::BulletText("The first visit to each place pauses briefly while shaders are built; later visits don't.");
    }
    EndCard();
    if (BeginCard("about", ImVec2(0, 0), false)) {
        Heading("About");
        const std::string& v = a.version;
        Dim("LittleBigPlanet 3 Launcher 3.0%s%s, for the native port (GPL-2.0-or-later).", v.empty() ? "" : " - ", v.c_str());
        Dim("Settings: %s", SettingsFile(FindRoot(LauncherDir(), "")).string().c_str());
        Dim("Ideas from LBPPC (github.com/emurphy04/LBPPC); this launcher is our own code, apart from the Quit Game edit (a port of "
            "LBPPC's quit_menu.py, GPL-2.0-or-later) and shadPS4's shadNet client (GPL-2.0-or-later). The game's art shown here "
            "comes from your own dump and is never copied anywhere.");
    }
    EndCard();
}

// ---------------------------------------------------------------------------------------------
// The setup (first start, or from Help): for a friend with a fresh folder too.

namespace {

struct DumpVerdict {
    bool ok = false;
    std::string title, line;
};
// The folder that holds CUSA00473 (CUSA00473 itself is accepted: its parent is used).
DumpVerdict CheckDumpFolder(fs::path& folder) {
    DumpVerdict v;
    std::error_code ec;
    if (folder.filename() == "CUSA00473" || fs::exists(folder / "sce_sys" / "param.sfo", ec)) folder = folder.parent_path();
    const fs::path game = folder / "CUSA00473";
    if (!fs::exists(game, ec)) {
        v.line = "No CUSA00473 folder in " + folder.string() + ". Choose the folder that holds it (or CUSA00473 itself).";
        return v;
    }
    const Sfo sfo = ReadSfo(game / "sce_sys" / "param.sfo");
    if (!sfo.ok) {
        v.line = "CUSA00473\\sce_sys\\param.sfo is missing or unreadable: is the dump complete?";
        return v;
    }
    const std::string id = sfo.Get("TITLE_ID"), ver = sfo.Get("APP_VER").empty() ? sfo.Get("VERSION") : sfo.Get("APP_VER");
    v.title = sfo.Get("TITLE") + " (" + id + ", " + ver + ")";
    if (id != "CUSA00473") {
        v.line = "This is " + id + ", not CUSA00473: this port needs the US edition of LittleBigPlanet 3.";
        return v;
    }
    if (ver != "01.28") {
        v.line = "This is version " + ver + ": the port needs 01.28 (the last update). Install the 01.28 update into the dump.";
        return v;
    }
    if (!fs::exists(game / "eboot.bin", ec)) {
        v.line = "CUSA00473\\eboot.bin is missing.";
        return v;
    }
    v.ok = true;
    v.line = "LittleBigPlanet 3 (US, 01.28): ready.";
    return v;
}

}  // namespace

void Wizard(App& a) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(vp->WorkSize.x - S(60), S(820)), std::min(vp->WorkSize.y - S(60), S(580))));
    ImGui::OpenPopup("Setup");
    if (!ImGui::BeginPopupModal("Setup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) return;
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        StitchedRect(dl, ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                     Col(kStitch, 0.45f), S(8), S(14));
    }
    const char* steps[] = {"Welcome", "Game files", "DLC", "Build", "Online", "Shaders", "Done"};
    const int nSteps = 7;
    // portable: no build to choose (the one beside the launcher)
    if (a.portable && a.wizard_step == 3) a.wizard_step = a.shot_mode ? 3 : 4;
    for (int i = 0; i < nSteps; ++i) {
        if (a.portable && i == 3) continue;
        if (i) ImGui::SameLine(), ImGui::TextColored(kDim, ">"), ImGui::SameLine();
        if (i == a.wizard_step) ImGui::TextColored(kCardboard, "%s", steps[i]);
        else ImGui::TextColored(i < a.wizard_step ? kText : kDim, "%s", steps[i]);
    }
    ImGui::Separator();
    ImGui::BeginChild("wizbody", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - S(14)), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
    std::error_code ec;
    switch (a.wizard_step) {
    case 0:
        ImGui::PushFont(g_heading, 30.f);
        ImGui::TextUnformatted("Welcome!");
        ImGui::PopFont();
        ImGui::TextWrapped("This launcher starts LittleBigPlanet 3 (the native PC port) with its settings, and looks after your "
                           "saves, DLC, online account and updates. A few quick steps check that everything the game needs is in "
                           "place. You can skip this and run it again from Help.");
        ImGui::Spacing();
        Dim("You need your own LittleBigPlanet 3 dump (US, CUSA00473, updated to 01.28). The port never changes its files.");
        break;
    case 1: {
        ImGui::PushFont(g_heading, 24.f);
        ImGui::TextUnformatted("Your game files");
        ImGui::PopFont();
        ImGui::TextWrapped("Choose the folder that holds CUSA00473 (choosing CUSA00473 itself works too).");
        fs::path folder = a.p.game_dir;
        const DumpVerdict v = folder.empty() ? DumpVerdict{false, "", "No folder chosen yet."} : CheckDumpFolder(folder);
        if (!a.p.game_dir.empty()) {
            ImGui::TextColored(v.ok ? kGood : kWarn, v.ok ? "Found:" : "Not ready:");
            ImGui::SameLine();
            ImGui::TextUnformatted(a.p.game_dir.string().c_str());
            if (!v.title.empty()) Dim("%s", v.title.c_str());
        }
        ImGui::TextColored(v.ok ? kGood : kWarn, "%s", v.line.c_str());
        // the folder dialog, or a typed (or pasted) path for when the dialog misbehaves
        auto use = [&a](fs::path f) {
            const DumpVerdict pv = CheckDumpFolder(f);  // CUSA00473 itself becomes its parent
            std::string error;
            if (!pv.ok) ToastMsg(a, pv.line, kWarn);
            if (WriteGameDir(a.p.root, f, &error)) {  // written when there is a CUSA00473, even one to fix
                AppendLauncherLog(a.p, "game_dir.txt set to " + f.string());
                Refresh(a, true);
                a.art_from.clear();
                a.dlc_dirty = true;
            } else {
                ToastMsg(a, error, kBad);
            }
        };
        const bool can_write = !a.shot_mode && !a.p.root.empty();
        ImGui::BeginDisabled(!can_write);
        if (BigButton("Choose the folder...", ImVec2(S(240), 0), kCardboard)) {
            const std::string picked = PickFolder(L"Choose the folder that holds CUSA00473");
            if (!picked.empty()) use(fs::u8path(picked));
        }
        ImGui::EndDisabled();
        Help("Writes the folder to game_dir.txt in the game folder, which is where the game looks (the previous one is kept as game_dir.txt.bak).");
        Dim("Or type or paste its path:");
        ImGui::BeginDisabled(!can_write);
        // (not EnterReturnsTrue: with it, InputString only stores the text when Enter is pressed)
        InputString("##dumpdir", a.wizard_dir, "for example D:\\Games\\PS4", 420);
        const bool entered =
            ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
        ImGui::SameLine();
        const std::string typed = TrimPathText(a.wizard_dir);
        ImGui::BeginDisabled(typed.empty());
        if ((ImGui::Button("Use this folder") || entered) && !typed.empty()) use(fs::u8path(typed));
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::BeginDisabled(!v.ok || a.file_check.running || a.shot_mode);
        if (ImGui::Button("Check the files")) StartFileCheck(a);
        ImGui::EndDisabled();
        {
            std::lock_guard lock(a.file_check.m);
            if (a.file_check.running) ImGui::SameLine(), Spinner(S(8), kCardboard);
            else if (a.file_check.done) ImGui::SameLine(), ImGui::TextColored(a.file_check.ok ? kGood : kBad, "%s", a.file_check.message.c_str());
        }
        break;
    }
    case 2: {
        ImGui::PushFont(g_heading, 24.f);
        ImGui::TextUnformatted("DLC");
        ImGui::PopFont();
        if (a.dlc_dirty) a.dlc = ListDlc(a.p.dlc_dir), a.dlc_dirty = false;  // not every frame
        const auto& packs = a.dlc;
        ImGui::TextWrapped("DLC you own goes in a folder of its own, one folder per pack. The game reads it every time it starts.");
        ImGui::TextColored(kCardboard, "%s", a.p.dlc_dir.string().c_str());
        ImGui::TextColored(packs.empty() ? kDim : kGood, "%d packs installed", static_cast<int>(packs.size()));
        ImGui::BeginDisabled(a.shot_mode);
        if (ImGui::Button("Use another folder...")) {
            const std::string picked = PickFolder(L"Choose your DLC folder");
            if (!picked.empty()) a.s.dlc_dir = picked, Refresh(a, true), a.dlc_dirty = true;
        }
        if (!a.s.dlc_dir.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Back to the default")) a.s.dlc_dir.clear(), Refresh(a, true), a.dlc_dirty = true;
        }
        ImGui::EndDisabled();
        Dim("To add packs from .pkg files, use the DLC page after the setup.");
        break;
    }
    case 3: {
        ImGui::PushFont(g_heading, 24.f);
        ImGui::TextUnformatted("Which build to play");
        ImGui::PopFont();
        const char* builds[] = {"This launcher's build", "Best of both worlds", "Port (as play.bat)"};
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
        ImGui::PushFont(g_heading, 24.f);
        ImGui::TextUnformatted("Online");
        ImGui::PopFont();
        if (a.acct.file.ok()) {
            ImGui::TextWrapped("You have an online account: %s%s%s.", a.acct.file.name.c_str(), a.acct.file.server.empty() ? "" : " on ",
                               a.acct.file.server.c_str());
            Toggle("Play online with it", &a.s.online, "Signs in to the server (levels, profiles) and shadNet (friends, playing together) when the game starts.");
            break;
        }
        ImGui::TextWrapped("Online play uses LBPPC's community server (play.lbppc.com; they agreed to this port using it). It is "
                           "optional, and you can set it up later on the Online page.");
        ImGui::RadioButton("Make a new account", &a.wizard_online_choice, 0);
        ImGui::SameLine();
        ImGui::RadioButton("I have one", &a.wizard_online_choice, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Not now", &a.wizard_online_choice, 2);
        if (a.wizard_online_choice == 2) break;
        ImGui::SetNextItemWidth(S(300));
        ImGui::InputTextWithHint("Online name", "3-16 letters, digits, - or _", a.wizard_name, sizeof a.wizard_name);
        const bool name_ok = ValidOnlineName(a.wizard_name);
        if (a.wizard_online_choice == 1) {
            ImGui::SetNextItemWidth(S(300));
            ImGui::InputTextWithHint("Password", "your account's password", a.wizard_pass, sizeof a.wizard_pass, ImGuiInputTextFlags_Password);
            // LBPPC's shadNet takes passwords of 8 characters or more (0.1.3).
            const bool pass_ok = ValidAccountPassword(a.wizard_pass);
            if (*a.wizard_pass && !pass_ok) ImGui::TextColored(kWarn, "8-64 characters, no spaces at either end.");
            ImGui::BeginDisabled(!name_ok || !pass_ok || a.shot_mode);
            if (BigButton("Save it", ImVec2(S(160), 0), kPlay)) {
                AccountFile f;
                f.name = a.wizard_name;
                f.password = a.wizard_pass;
                f.server = "play.lbppc.com";
                std::string error;
                const fs::path cfg = a.p.root / "user" / "config";
                if (WriteAccountFile(NewAccountFile(cfg, f.name), f, &error)) {
                    ToastMsg(a, "Account saved. Online > Test sign-in checks it.", kGood);
                    a.s.online = true;
                    std::memset(a.wizard_pass, 0, sizeof a.wizard_pass);
                    Refresh(a, true);
                } else {
                    ToastMsg(a, error, kBad);
                }
            }
            ImGui::EndDisabled();
            Help("Saved only on this PC, in user\\config\\online_account.txt; the game reads it from there.");
        } else {
            Dim("A strong password is made for you and saved in user\\config\\online_account.txt before anything is sent.");
            ImGui::BeginDisabled(!name_ok || a.online.create.running || a.shot_mode);
            if (BigButton("Create the account", ImVec2(S(220), 0), kPlay)) StartCreateAccount(a, a.wizard_name);
            ImGui::EndDisabled();
            std::lock_guard lock(a.online.create.m);
            if (a.online.create.running) ImGui::SameLine(), Spinner(S(8), kCardboard);
            for (const auto& l : a.online.create.lines) Dim("%s", l.c_str());
        }
        break;
    }
    case 5:
        ImGui::PushFont(g_heading, 24.f);
        ImGui::TextUnformatted("A note on shaders");
        ImGui::PopFont();
        ImGui::TextWrapped("The first time you visit each place in the game, it pauses for a moment while your graphics card's "
                           "shaders are built. That happens once: they are kept (user\\shader_cache), and later visits are smooth.");
        break;
    default:
        ImGui::PushFont(g_heading, 30.f);
        ImGui::TextUnformatted("All set");
        ImGui::PopFont();
        ImGui::TextWrapped("Press PLAY to start the game. The pages on the left hold the settings; Help has this setup and a short tour.");
        if (!EssentialsOk(a)) ImGui::TextColored(kWarn, "Something is still missing; the Tools page lists it.");
        break;
    }
    ImGui::EndChild();
    if (BigButton("Skip", ImVec2(S(100), 0), kCard)) a.wizard = false, a.s.setup_done = true, a.tour = !a.s.tour_done, ImGui::CloseCurrentPopup();
    ImGui::SameLine(ImGui::GetWindowWidth() - S(270));
    if (BigButton("Back", ImVec2(S(120), 0), kCard, a.wizard_step > 0)) {
        --a.wizard_step;
        if (a.portable && a.wizard_step == 3) --a.wizard_step;
    }
    ImGui::SameLine();
    if (a.wizard_step < nSteps - 1) {
        if (BigButton("Next", ImVec2(S(120), 0), kCardboard)) {
            ++a.wizard_step;
            if (a.portable && a.wizard_step == 3) ++a.wizard_step;
        }
    } else if (BigButton("Finish", ImVec2(S(120), 0), kPlay)) {
        a.wizard = false;
        a.s.setup_done = true;
        a.tour = !a.s.tour_done;
        a.tour_step = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------------------------
// The tour: a few bubbles pointing at the launcher's parts.

void Tour(App& a) {
    struct Step {
        const char* mark;
        const char* title;
        const char* text;
    };
    static const Step steps[] = {
        {"play", "PLAY", "Starts the game with your settings. While it runs, the launcher gets out of the way."},
        {"pill", "Your status", "Offline, online as you, signing in, or playing: always here at a glance."},
        {"page_online", "Online", "Server status, a sign-in test that explains problems in plain words, and your accounts."},
        {"page_community", "Community", "The server's levels: team picks, the newest, the most loved. Queue one for later."},
        {"page_help", "Help", "This tour, the setup, and what to do when something goes wrong."},
    };
    const int n = IM_ARRAYSIZE(steps);
    if (a.tour_step >= n) {
        a.tour = false;
        a.s.tour_done = true;
        return;
    }
    const Step& st = steps[a.tour_step];
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 v0 = vp->Pos, v1(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    ImVec2 mn(v0.x + vp->Size.x * 0.4f, v0.y + vp->Size.y * 0.4f), mx(mn.x + S(10), mn.y + S(10));
    if (auto it = a.marks.find(st.mark); it != a.marks.end()) mn = it->second.first, mx = it->second.second;
    const float pad = S(8);
    mn.x -= pad, mn.y -= pad, mx.x += pad, mx.y += pad;
    (void)dl;
    // the bubble beside it
    const float bw = S(380);
    ImVec2 bp(mx.x + S(24), mn.y);
    if (bp.x + bw > v1.x - S(20)) bp.x = mn.x - bw - S(24);
    if (bp.x < v0.x + S(20)) bp = ImVec2(std::clamp(mn.x, v0.x + S(20), v1.x - bw - S(20)), mx.y + S(20));
    bp.y = std::clamp(bp.y, v0.y + S(20), v1.y - S(220));
    ImGui::SetNextWindowPos(bp);
    ImGui::SetNextWindowSize(ImVec2(bw, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(kCard.x, kCard.y, kCard.z, 0.99f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(14));
    ImGui::Begin("##tour", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    {
        // The shade around the highlighted part, drawn first in this window (which is above the
        // page), so it dims the launcher but not this bubble.
        ImDrawList* wl = ImGui::GetWindowDrawList();
        wl->PushClipRectFullScreen();
        const ImU32 shade = Col(ImVec4(0.02f, 0.015f, 0.01f, 0.66f));
        wl->AddRectFilled(v0, ImVec2(v1.x, mn.y), shade);
        wl->AddRectFilled(ImVec2(v0.x, mx.y), v1, shade);
        wl->AddRectFilled(ImVec2(v0.x, mn.y), ImVec2(mn.x, mx.y), shade);
        wl->AddRectFilled(ImVec2(mx.x, mn.y), ImVec2(v1.x, mx.y), shade);
        const float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(a.time) * 4.f);
        wl->AddRect(mn, mx, Col(kOrange, pulse), S(12), S(3));
        StitchedRect(wl, ImVec2(mn.x - S(6), mn.y - S(6)), ImVec2(mx.x + S(6), mx.y + S(6)), Col(kStitch, 0.8f), 0, S(14));
        // the bubble's own background over the shade
        const ImVec2 bp0 = ImGui::GetWindowPos(), bp1(bp0.x + ImGui::GetWindowSize().x, bp0.y + ImGui::GetWindowSize().y);
        if (ImGui::GetWindowSize().x > 0) wl->AddRectFilled(bp0, bp1, Col(kCard, 1.f), S(14));
        wl->PopClipRect();
    }
    StitchedRect(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                 Col(kStitch, 0.5f), S(6), S(14));
    ImGui::TextColored(kDim, "%d / %d", a.tour_step + 1, n);
    ImGui::PushFont(g_heading, 24.f);
    ImGui::TextColored(kCardboard, "%s", st.title);
    ImGui::PopFont();
    ImGui::TextWrapped("%s", st.text);
    ImGui::Spacing();
    if (BigButton(a.tour_step + 1 < n ? "Next" : "Got it", ImVec2(S(120), 0), kCardboard)) ++a.tour_step;
    ImGui::SameLine();
    if (BigButton("Skip the tour", ImVec2(S(150), 0), kCard)) a.tour = false, a.s.tour_done = true;
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------------------------

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
            if (BigButton("Restore", ImVec2(S(150), 0), kOrange)) {
                std::string error, safety;
                if (GameRunning()) ToastMsg(a, "Close the game first.", kWarn);
                else if (RestoreBackup(a.p, bk, &error, &safety)) {
                    ToastMsg(a, "Restored " + bk.name + (safety.empty() ? "" : " (your previous saves: " + safety + ")"), kGood);
                    AppendLauncherLog(a.p, "restored " + bk.name + ", previous saves kept as " + safety);
                } else ToastMsg(a, "Restore failed: " + error, kBad);
                a.restore_index = -1;
                Refresh(a, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (BigButton("Cancel", ImVec2(S(150), 0), kCard)) a.restore_index = -1, ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace launcher::ui
