// The online side of LBP3Launcher (launcher_app.h): the Online hub (server status, the sign-in
// test, accounts, the server's news, Discord), Friends (shadNet, only while that page is open and
// the game isn't running) and Community (the server's levels, read-only but for "Queue").
// Nothing here talks to a server unless the player opens the page or presses a button; opening
// the Online page asks the server once whether it is there (its tooltip says so).
#include "launcher_app.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "launcher_discord.h"
#include "launcher_shell.h"
#include "launcher_theme.h"

namespace launcher::ui {
namespace {

ImTextureRef Ref(const Texture& t) { return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(t.tex))); }

fs::path ConfigDir(const App& a) { return a.p.root / "user" / "config"; }

void ReadAccounts(App& a, bool now = false) {
    if (!now && ImGui::GetTime() - a.online.accounts_read < 2.0) return;
    a.online.accounts = ListAccounts(ConfigDir(a));
    a.online.accounts_read = ImGui::GetTime();
}

void StartPing(App& a) {
    OnlineState& o = a.online;
    const std::string server = OnlineServer(a);
    if (o.ping.running || server.empty()) return;
    o.pinged = true;
    o.ping.Start();
    std::thread([&o, server] {
        ServerInfo info = PingServer(server);
        std::string error;
        auto news = info.ok ? FetchAnnouncements(server, &error) : std::vector<Announcement>{};
        {
            std::lock_guard lock(o.ping.m);
            o.info = std::move(info);
            o.news = std::move(news);
            o.news_error = error;
        }
        o.ping.Finish(o.info.ok, o.info.ok ? "reachable" : o.info.error);
    }).detach();
}

void StartSignInTest(App& a) {
    OnlineState& o = a.online;
    if (o.test.running || !a.acct.file.ok()) return;
    const AccountFile acct = a.acct.file;
    const Settings eff = Eff(a);
    const ShadnetEndpoint at = ParseShadnetServer(ShadnetServer(eff));
    const std::string server = OnlineServer(a);
    o.test.Start();
    {
        std::lock_guard lock(o.test.m);
        o.test_shadnet = o.test_web = "";
    }
    std::thread([&o, acct, at, server] {
        std::string sn = at.ok() ? TestShadnetSignIn(at, acct.name, acct.password) : "no shadNet server for this account's server";
        const WebLogin web = server.empty() ? WebLogin{} : RefreshLogin(server, AccountEmail(acct), acct.password);
        std::lock_guard lock(o.test.m);
        o.test_shadnet_ok = sn.empty();
        o.test_shadnet = sn.empty() ? "signed in" : sn;
        o.test_web_ok = web.ok;
        o.test_web = server.empty() ? "no server" : web.ok ? "signed in" : web.error;
        o.test.running = false;
        o.test.done = true;
        o.test.ok = o.test_shadnet_ok && o.test_web_ok;
    }).detach();
}

void ResultLine(const char* what, bool ok, const std::string& text) {
    if (text.empty()) return;
    ImGui::TextColored(ok ? kGood : kBad, ok ? "OK" : "X ");
    ImGui::SameLine();
    ImGui::TextUnformatted(what);
    ImGui::SameLine(S(130));
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextColored(ok ? kText : kWarn, "%s", text.c_str());
    ImGui::PopTextWrapPos();
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// The Online hub

void PageOnline(App& a) {
    Settings& s = a.s;
    OnlineState& o = a.online;
    ReadAccounts(a);
    if (!o.pinged) StartPing(a);  // once per opening: is the server there? (the tooltip says so)
    const Settings eff = Eff(a);
    const std::string server = OnlineServer(a);
    PageTitle("Online", "Play on a community server: your accounts, the server's status and news.");
    const float w = ImGui::GetContentRegionAvail().x, half = (w - S(12)) * 0.5f;

    if (BeginCard("switch", ImVec2(half, S(250)))) {
        Heading("Play online");
        Toggle("Online", &s.online,
               "When the game starts it signs in to the server with your active account: its levels and profiles (Refresh) and "
               "shadNet (sign-in, friends, playing together). The password is read by the game from your account file.");
        if (a.acct.file.ok()) {
            ImGui::TextColored(kDim, "as");
            ImGui::SameLine();
            ImGui::PushFont(g_bold, 18.f);
            ImGui::TextColored(kCardboard, "%s", a.acct.file.name.c_str());
            ImGui::PopFont();
            Dim("on %s (shadNet: %s)", NormalizeServer(eff.online_server).c_str(), ShadnetServer(eff).c_str());
        } else if (!a.acct.exists) {
            ImGui::TextColored(kWarn, "No account yet: create one under Accounts.");
        } else {
            ImGui::TextColored(kWarn, "The active account file has no name or password.");
        }
        ImGui::Spacing();
        const bool can_test = a.acct.file.ok() && !o.test.running && !GameBusy(a) && !a.friends.session.Active() && !a.shot_mode;
        ImGui::BeginDisabled(!can_test);
        if (BigButton("Test sign-in", ImVec2(S(170), 0), kCardboard)) StartSignInTest(a);
        ImGui::EndDisabled();
        Help("Signs in once to shadNet and to the server's website with your active account, then out again, and says in "
             "plain words what went wrong if anything did. Not while the game runs or Friends is open (an account can't be "
             "signed in twice).");
        if (o.test.running) ImGui::SameLine(), Spinner(S(9), kCardboard);
        std::lock_guard lock(o.test.m);
        ResultLine("shadNet", o.test_shadnet_ok, o.test_shadnet);
        ResultLine("Website", o.test_web_ok, o.test_web);
    }
    EndCard();
    ImGui::SameLine();
    if (BeginCard("status", ImVec2(half, S(250)))) {
        Heading("Server");
        {
            std::lock_guard lock(o.ping.m);
            if (server.empty()) Dim("No server yet: an account (or the server below) sets it.");
            else if (o.ping.running) {
                Spinner(S(9), kCardboard);
                ImGui::SameLine();
                Dim("asking %s...", server.c_str());
            } else if (o.ping.done && o.info.ok) {
                ImGui::PushFont(g_bold, 20.f);
                ImGui::TextColored(kGood, "%s", o.info.name.empty() ? server.c_str() : o.info.name.c_str());
                ImGui::PopFont();
                Dim("Reachable (%d ms) - %s %s", o.info.ms, o.info.software.c_str(), o.info.version.c_str());
                if (o.info.maintenance) ImGui::TextColored(kWarn, "Down for maintenance right now.");
                if (!o.info.description.empty()) {
                    ImGui::PushTextWrapPos(0.f);
                    ImGui::TextColored(kDim, "%s", o.info.description.c_str());
                    ImGui::PopTextWrapPos();
                }
            } else if (o.ping.done) {
                ImGui::TextColored(kBad, "Not reachable");
                Dim("%s", o.info.error.c_str());
            }
        }
        ImGui::BeginDisabled(o.ping.running || server.empty());
        if (ImGui::SmallButton("Check again")) StartPing(a);
        ImGui::EndDisabled();
        Help("Opening this page asks the server once whether it is there (one small request, nothing about you is sent).");
        if (!a.acct.file.ok()) {
            InputString("Server", s.online_server, "play.lbppc.com", 300);
            Help("Used only without an account file; an account carries its own server.");
        }
    }
    EndCard();

    if (BeginCard("accounts", ImVec2(0, 0), false)) {
        Heading("Accounts");
        Dim("Accounts saved on this PC (user\\config\\online_account*.txt). The game uses the active one.");
        if (o.accounts.empty()) Dim("None yet.");
        for (size_t i = 0; i < o.accounts.size(); ++i) {
            const AccountEntry& e = o.accounts[i];
            ImGui::PushID(static_cast<int>(i));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(p.x + S(8), p.y + ImGui::GetTextLineHeight() * 0.5f + S(2)), S(5), Col(e.active ? kGood : kDim, e.active ? 1.f : 0.5f));
            ImGui::SetCursorScreenPos(ImVec2(p.x + S(22), p.y));
            ImGui::PushFont(e.active ? g_bold : g_font, 17.f);
            ImGui::TextUnformatted(e.data.name.empty() ? "(no name)" : e.data.name.c_str());
            ImGui::PopFont();
            ImGui::SameLine(S(230));
            Dim("%s%s", e.data.server.empty() ? "(no server noted)" : e.data.server.c_str(), e.active ? "  - active" : "");
            ImGui::SameLine(S(520));
            ImGui::BeginDisabled(a.shot_mode);
            if (!e.active) {
                ImGui::BeginDisabled(GameBusy(a) || a.friends.session.Active());
                if (ImGui::SmallButton("Use this one")) {
                    std::string error;
                    if (SetActiveAccount(ConfigDir(a), e.file, &error)) ToastMsg(a, "Now playing as " + e.data.name, kGood);
                    else ToastMsg(a, "Couldn't switch: " + error, kBad);
                    Refresh(a, true);
                    ReadAccounts(a, true);
                    o.test_shadnet = o.test_web = "";
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("Change saved password")) {
                o.password_for = static_cast<int>(i);
                std::memset(o.password_buf, 0, sizeof o.password_buf);
                o.password_show = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Open")) OpenPath(e.file);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("New account");
        ImGui::SameLine(S(130));
        ImGui::SetNextItemWidth(S(260));
        ImGui::InputTextWithHint("##newname", "an online name (3-16 letters, digits, - _)", o.create_name, sizeof o.create_name);
        ImGui::SameLine();
        const bool name_ok = ValidOnlineName(o.create_name);
        ImGui::BeginDisabled(!name_ok || o.create.running || a.shot_mode);
        if (BigButton("Create account", ImVec2(S(180), 0), kPlay)) o.create_open = true;
        ImGui::EndDisabled();
        Help("Makes your shadNet account (sign-in, friends, playing together) and your website account (levels, profiles) on "
             "play.lbppc.com with one strong random password, saved on this PC before anything is sent.");
        std::lock_guard lock(o.create.m);
        if (o.create.running) ImGui::SameLine(), Spinner(S(9), kCardboard);
        for (const auto& l : o.create.lines) Dim("%s", l.c_str());
        if (o.create.done) ImGui::TextColored(o.create.ok ? kGood : kBad, "%s", o.create.message.c_str());
    }
    EndCard();

    if (BeginCard("news", ImVec2(0, 0), false)) {
        Heading("Server news");
        {
            std::lock_guard lock(o.ping.m);
            if (o.news.empty()) Dim("%s", o.ping.running ? "..." : o.news_error.empty() ? "No announcements." : o.news_error.c_str());
            for (const auto& n : o.news) {
                ImGui::TextColored(kCardboard, "%s", n.title.c_str());
                ImGui::SameLine();
                Dim("%s", n.created.c_str());
                ImGui::PushTextWrapPos(0.f);
                ImGui::TextUnformatted(n.text.c_str());  // server text: never a format string
                ImGui::PopTextWrapPos();
            }
        }
        Heading("Latest port changes");
        std::lock_guard lock(a.news_m);
        for (size_t i = 0; i < a.news.size() && i < 3; ++i) {
            ImGui::TextColored(kCardboard, "%s", a.news[i].when.c_str());
            ImGui::SameLine(S(108));
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(a.news[i].subject.c_str());
            ImGui::PopTextWrapPos();
        }
        if (a.news.empty()) Dim("(What's new has the full list.)");
    }
    EndCard();

    if (BeginCard("discord", ImVec2(0, 0), false)) {
        Heading("Discord (off unless switched on)");
        Toggle("Show \"Playing LittleBigPlanet 3\" on Discord", &s.discord,
               "While the game runs (started from here), Discord shows the game, the time played and the level you're in. "
               "Uses Discord's local connection on this PC.");
        InputString("Discord application ID", s.discord_app_id, "your own application's ID (17-20 digits)", 340);
        Help("Discord shows the name and picture of the application this ID belongs to, so it must be yours: on "
             "discord.com/developers/applications press New Application, name it (for example \"LittleBigPlanet 3\"), then "
             "copy its Application ID here. Never use another project's ID.");
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
        std::lock_guard lock(a.discord_test.m);
        if (a.discord_test.running) ImGui::SameLine(), Dim("talking to Discord...");
        else if (a.discord_test.done) ImGui::SameLine(), ImGui::TextColored(a.discord_test.ok ? kGood : kWarn, "%s", a.discord_test.message.c_str());
    }
    EndCard();
}

void OnlineModals(App& a) {
    OnlineState& o = a.online;
    // change the saved password
    if (o.password_for >= 0) ImGui::OpenPopup("Change the saved password");
    if (ImGui::BeginPopupModal("Change the saved password", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (o.password_for < 0 || o.password_for >= static_cast<int>(o.accounts.size())) {
            o.password_for = -1;
            ImGui::CloseCurrentPopup();
        } else {
            const AccountEntry& e = o.accounts[o.password_for];
            ImGui::Text("The password this PC keeps for %s", e.data.name.c_str());
            ImGui::PushTextWrapPos(S(520));
            Dim("Only this PC's copy changes; the servers keep theirs. Use this when the saved one is wrong (Test sign-in says "
                "\"wrong password\"), with the password the account really has.");
            ImGui::PopTextWrapPos();
            ImGui::SetNextItemWidth(S(360));
            ImGui::InputTextWithHint("##pw", "the account's password", o.password_buf, sizeof o.password_buf,
                                     o.password_show ? 0 : ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            ImGui::Checkbox("Show", &o.password_show);
            const bool ok = ValidAccountPassword(o.password_buf);
            if (*o.password_buf && !ok) ImGui::TextColored(kWarn, "8-64 characters, no spaces at either end.");
            ImGui::BeginDisabled(!ok);
            if (BigButton("Save", ImVec2(S(140), 0), kPlay)) {
                std::string error;
                if (SetAccountPassword(e.file, o.password_buf, &error)) ToastMsg(a, "Saved. Test sign-in checks it.", kGood);
                else ToastMsg(a, "Couldn't save it: " + error, kBad);
                std::memset(o.password_buf, 0, sizeof o.password_buf);
                o.password_for = -1;
                Refresh(a, true);
                ReadAccounts(a, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (BigButton("Cancel", ImVec2(S(140), 0), kCard)) {
                std::memset(o.password_buf, 0, sizeof o.password_buf);
                o.password_for = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    // create both accounts
    if (o.create_open) ImGui::OpenPopup("Create the accounts?"), o.create_open = false;
    if (ImGui::BeginPopupModal("Create the accounts?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Create \"%s\" on play.lbppc.com?", o.create_name);
        ImGui::PushTextWrapPos(S(520));
        Dim("Your online name goes to LBPPC's shadNet and website. A strong password is made here and saved on this PC first; "
            "shadNet gets it over an encrypted connection, the website only a hash of it.");
        ImGui::PopTextWrapPos();
        if (BigButton("Create", ImVec2(S(150), 0), kPlay)) {
            o.create.Start();
            const std::string name = o.create_name, dir = ConfigDir(a).string();
            const Paths p = a.p;
            std::thread([&a, name, dir, p] {
                const CreateResult r = CreateOnlineAccounts(dir, "https://play.lbppc.com", ParseShadnetServer("play.lbppc.com"), name,
                                                            RandomPassword(20), [&](const std::string& l) { a.online.create.Line(l); });
                AppendLauncherLog(p, "create account \"" + name + "\": " + r.message);  // never the password
                a.online.create.Finish(r.ok, r.message);
            }).detach();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (BigButton("Cancel", ImVec2(S(150), 0), kCard)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    {
        std::lock_guard lock(o.create.m);
        if (o.create.done && o.create.ok && !o.create.message.empty()) {
            ToastMsg(a, o.create.message, kGood);
            o.create.message += " ";  // shown once
            a.s.online = true;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Friends (shadNet)

void PageFriends(App& a) {
    FriendsState& f = a.friends;
    PageTitle("Friends", "Who's online, friend requests, and adding friends by name (shadNet).");
    const FriendsSession::View v = f.session.Snapshot();
    if (GameBusy(a)) {
        if (BeginCard("busy", ImVec2(0, 0), false)) {
            ImGui::TextColored(kInfo, "The game is running, and it signs in to shadNet itself.");
            Dim("An account can only be signed in once, so the friends list is here while the game is closed. In the game, "
                "your friends are in the Pod's Community menu.");
        }
        EndCard();
        return;
    }
    if (!a.acct.file.ok()) {
        if (BeginCard("noacct", ImVec2(0, 0), false)) {
            ImGui::TextColored(kWarn, "No online account yet.");
            Dim("Create one on the Online page (or save the one you have), then come back here.");
            if (ImGui::Button("Go to Online")) a.page = Online;
        }
        EndCard();
        return;
    }
    const ShadnetEndpoint at = ParseShadnetServer(ShadnetServer(Eff(a)));
    // signed in only while this page is open (Frame signs out when it closes)
    if (v.status == FriendsSession::Status::Off && !f.stopping && !a.shot_mode && at.ok()) f.session.Start(at, a.acct.file.name, a.acct.file.password);

    if (BeginCard("who", ImVec2(0, 0), false)) {
        switch (v.status) {
        case FriendsSession::Status::SigningIn:
            Spinner(S(9), kCardboard);
            ImGui::SameLine();
            ImGui::TextUnformatted(("Signing in as " + a.acct.file.name + "...").c_str());
            break;
        case FriendsSession::Status::SignedIn:
            ImGui::TextColored(kGood, "Signed in as %s", v.name.c_str());
            ImGui::SameLine();
            Dim("(signed out again when you leave this page or press PLAY)");
            break;
        case FriendsSession::Status::Failed:
            ImGui::TextColored(kBad, "Couldn't sign in: %s", v.error.c_str());
            if (ImGui::Button("Try again")) f.session.Stop(), f.session.Start(at, a.acct.file.name, a.acct.file.password);
            ImGui::SameLine();
            if (ImGui::Button("Accounts")) a.page = Online;
            break;
        default:
            Dim(a.shot_mode ? "(the pictures don't sign in)" : "Not signed in.");
            break;
        }
        if (!v.note.empty()) ImGui::TextColored(kCardboard, "%s", v.note.c_str());
    }
    EndCard();
    const bool on = v.status == FriendsSession::Status::SignedIn;
    if (BeginCard("add", ImVec2(0, 0), false)) {
        Heading("Add a friend");
        ImGui::SetNextItemWidth(S(300));
        const bool enter = ImGui::InputTextWithHint("##add", "their online name", f.add_name, sizeof f.add_name, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(!on || !ValidOnlineName(f.add_name));
        if (BigButton("Send request", ImVec2(S(170), 0), kCardboard) || (enter && on && ValidOnlineName(f.add_name))) {
            f.session.Add(f.add_name);
            std::memset(f.add_name, 0, sizeof f.add_name);
        }
        ImGui::EndDisabled();
    }
    EndCard();
    if (!v.list.received.empty() && BeginCard("requests", ImVec2(0, 0), false)) {
        Heading("Friend requests");
        for (const auto& n : v.list.received) {
            ImGui::PushID(n.c_str());
            ImGui::PushFont(g_bold, 17.f);
            ImGui::TextUnformatted(n.c_str());
            ImGui::PopFont();
            ImGui::SameLine(S(260));
            ImGui::BeginDisabled(!on);
            if (ImGui::SmallButton("Accept")) f.session.Add(n);
            ImGui::SameLine();
            if (ImGui::SmallButton("Decline")) f.session.Remove(n);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        EndCard();
    } else if (v.list.received.empty()) {
        // (no card when there are no requests)
    }
    const float w = ImGui::GetContentRegionAvail().x;
    Heading(("Friends (" + std::to_string(v.list.friends.size()) + ")").c_str());
    if (v.list.friends.empty()) Dim(on ? "No friends yet: send a request by name above." : "");
    const float cw = std::max(S(220), (w - S(24)) / 3.f);
    int col = 0;
    for (const auto& fr : v.list.friends) {
        if (col) ImGui::SameLine();
        ImGui::PushID(fr.name.c_str());
        if (BeginCard("friend", ImVec2(cw, S(86)))) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(p.x + S(18), p.y + S(18)), S(18), Col(kCardboard, 0.25f));
            DrawIcon(dl, Icon::Friends, ImVec2(p.x + S(18), p.y + S(18)), S(20), Col(kCardboard));
            dl->AddCircleFilled(ImVec2(p.x + S(32), p.y + S(32)), S(6), Col(fr.online ? kGood : kDim, fr.online ? 1.f : 0.6f));
            ImGui::SetCursorScreenPos(ImVec2(p.x + S(48), p.y));
            ImGui::PushFont(g_bold, 17.f);
            ImGui::TextUnformatted(fr.name.c_str());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(p.x + S(48), p.y + S(24)));
            ImGui::TextColored(fr.online ? kGood : kDim, fr.online ? "online" : "offline");
            ImGui::SameLine(cw - S(110));
            ImGui::BeginDisabled(!on);
            if (f.confirm_remove == fr.name) {
                if (ImGui::SmallButton("Sure?")) f.session.Remove(fr.name), f.confirm_remove.clear();
            } else if (ImGui::SmallButton("Remove")) {
                f.confirm_remove = fr.name;
            }
            ImGui::EndDisabled();
        }
        EndCard();
        ImGui::PopID();
        col = (col + 1) % 3;
    }
    if (!v.list.sent.empty()) {
        Heading("Requests you sent");
        for (const auto& n : v.list.sent) {
            ImGui::PushID(n.c_str());
            ImGui::TextUnformatted(n.c_str());
            ImGui::SameLine(S(260));
            ImGui::BeginDisabled(!on);
            if (ImGui::SmallButton("Cancel")) f.session.Remove(n);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Community (the server's levels)

namespace {

const char* const kCategories[][2] = {
    {"teamPicks", "Team Picks"}, {"newest", "Newest"}, {"mostHearted", "Most Hearted"}, {"mostLiked", "Top Rated"}, {"mostPlayed", "Most Played"},
};

void StartLevels(App& a) {
    CommunityState& c = a.community;
    const std::string server = OnlineServer(a);
    if (c.job.running || server.empty()) return;
    c.loaded = true;
    c.selected = -1;
    c.job.Start();
    std::thread([&c, server, cat = c.query.empty() ? c.category : std::string("search"), q = c.query, skip = c.skip] {
        LevelPage page = FetchLevels(server, cat, q, skip, 24);
        const bool ok = page.error.empty();
        {
            std::lock_guard lock(c.m);
            c.page = std::move(page);
            // their icons, fetched one at a time by the icon worker
            for (const auto& l : c.page.levels)
                if (IsSha1Hex(l.icon_hash) && !c.icons.count(l.icon_hash) && !c.icon_bytes.count(l.icon_hash) &&
                    std::find(c.icon_queue.begin(), c.icon_queue.end(), l.icon_hash) == c.icon_queue.end())
                    c.icon_queue.push_back(l.icon_hash);
        }
        c.job.Finish(ok, ok ? "" : "");
    }).detach();
}

// The icons: one request at a time (the server limits how fast a client may ask), cached on disk
// (user\cache\launcher_icons) so a page seen before costs nothing.
void IconWorker(App& a) {
    CommunityState& c = a.community;
    if (c.icon_worker) return;
    {
        std::lock_guard lock(c.m);
        if (c.icon_queue.empty()) return;
    }
    c.icon_worker = true;
    const std::string server = OnlineServer(a);
    const fs::path cache = a.p.root / "user" / "cache" / "launcher_icons";
    std::thread([&c, server, cache] {
        for (;;) {
            std::string hash;
            {
                std::lock_guard lock(c.m);
                if (c.icon_queue.empty()) break;
                hash = c.icon_queue.front();
                c.icon_queue.erase(c.icon_queue.begin());
            }
            std::string png;
            std::error_code ec;
            const fs::path file = cache / (hash + ".png");
            if (fs::exists(file, ec)) png = ReadFile(file);
            if (png.empty()) {
                std::string error;
                png = FetchIcon(server, hash, &error);
                if (!png.empty()) {
                    fs::create_directories(cache, ec);
                    std::ofstream(file, std::ios::binary) << png;
                }
                Sleep(120);
            }
            std::lock_guard lock(c.m);
            c.icon_bytes[hash] = png;  // empty: none (a placeholder is drawn)
        }
        c.icon_worker = false;
    }).detach();
}

}  // namespace

void CommunityFrame(App& a) {
    CommunityState& c = a.community;
    std::map<std::string, std::string> ready;
    {
        std::lock_guard lock(c.m);
        ready.swap(c.icon_bytes);
    }
    for (auto& [hash, png] : ready) {
        Texture t;
        if (!png.empty()) t = MakeTexture(a.renderer, Downscale(LoadImageMemory(png.data(), png.size()), 256, 256));
        c.icons[hash] = t;
    }
    if (a.page == Community) IconWorker(a);
}

void PageCommunity(App& a) {
    CommunityState& c = a.community;
    const std::string server = OnlineServer(a);
    PageTitle("Community", server.empty() ? "The levels people made, from your community server." : ("Levels from " + server).c_str());
    if (server.empty()) {
        if (BeginCard("noserver", ImVec2(0, 0), false)) {
            ImGui::TextColored(kWarn, "No community server yet.");
            Dim("An online account (Online page) sets one; play.lbppc.com is LBPPC's.");
            if (ImGui::Button("Go to Online")) a.page = Online;
        }
        EndCard();
        return;
    }
    if (!c.loaded) StartLevels(a);  // on opening the page
    // categories and search
    for (const auto& [key, label] : kCategories) {
        const bool sel = c.query.empty() && c.category == key;
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(kCardboard.x, kCardboard.y, kCardboard.z, 0.85f) : ImVec4(0.235f, 0.188f, 0.149f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? ImVec4(0.1f, 0.07f, 0.04f, 1.f) : kText);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(20));
        if (ImGui::Button(label)) c.category = key, c.query.clear(), c.skip = 0, StartLevels(a);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(S(220));
    const bool enter = ImGui::InputTextWithHint("##search", "search levels", c.search, sizeof c.search, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Search") || enter) && *c.search) c.query = c.search, c.skip = 0, StartLevels(a);

    LevelPage page;
    {
        std::lock_guard lock(c.m);
        page = c.page;
    }
    {
        std::lock_guard lock(c.job.m);
        if (c.job.running) {
            Spinner(S(10), kCardboard);
            ImGui::SameLine();
            Dim("Loading...");
            return;
        }
    }
    if (!page.error.empty()) {
        ImGui::TextColored(kBad, "%s", page.error.c_str());
        if (ImGui::Button("Try again")) StartLevels(a);
        return;
    }
    if (page.levels.empty()) {
        Dim(c.query.empty() ? "No levels here yet." : "Nothing found.");
        return;
    }
    // the grid
    const float w = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(2, static_cast<int>(w / S(250)));
    const float cw = (w - S(12) * (cols - 1)) / cols;
    for (size_t i = 0; i < page.levels.size(); ++i) {
        const LevelCard& l = page.levels[i];
        if (i % cols) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        if (BeginCard("lvl", ImVec2(cw, S(268)))) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float iw = cw - S(36), ih = S(150);
            auto it = c.icons.find(l.icon_hash);
            if (it != c.icons.end() && it->second.tex) {
                dl->AddImageRounded(Ref(it->second), p, ImVec2(p.x + iw, p.y + ih), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, S(10));
            } else {
                dl->AddRectFilled(p, ImVec2(p.x + iw, p.y + ih), Col(kCardboard, 0.14f), S(10));
                DrawIcon(dl, Icon::Level, ImVec2(p.x + iw * 0.5f, p.y + ih * 0.5f), S(44), Col(kCardboard, 0.6f));
            }
            if (l.team_picked) {
                const ImVec2 b(p.x + S(8), p.y + S(8));
                dl->AddRectFilled(b, ImVec2(b.x + S(96), b.y + S(24)), Col(kOrange, 0.92f), S(12));
                DrawIcon(dl, Icon::Star, ImVec2(b.x + S(13), b.y + S(12)), S(13), Col(kText));
                dl->AddText(ImVec2(b.x + S(24), b.y + S(3)), Col(kText), "Team Pick");
            }
            ImGui::Dummy(ImVec2(iw, ih + S(4)));
            ImGui::PushFont(g_bold, 17.f);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iw);
            const std::string title = l.title.size() > 60 ? l.title.substr(0, 57) + "..." : l.title;
            ImGui::TextUnformatted(title.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ImGui::TextColored(kDim, "by");
            ImGui::SameLine();
            ImGui::TextColored(kCardboard, "%s", l.publisher.empty() ? "?" : l.publisher.c_str());
            const ImVec2 q = ImGui::GetCursorScreenPos();
            DrawIcon(dl, Icon::Heart, ImVec2(q.x + S(8), q.y + S(9)), S(14), Col(kBad));
            ImGui::SetCursorScreenPos(ImVec2(q.x + S(20), q.y));
            ImGui::Text("%d", l.hearts);
            ImGui::SameLine();
            DrawIcon(dl, Icon::Play, ImVec2(ImGui::GetCursorScreenPos().x + S(8), q.y + S(9)), S(12), Col(kDim));
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + S(18), q.y));
            ImGui::TextColored(kDim, "%d plays", l.plays);
        }
        EndCard();
        if (ImGui::IsItemClicked()) c.selected = static_cast<int>(i), ImGui::OpenPopup("level");
        if (c.selected == static_cast<int>(i) && ImGui::BeginPopupModal("level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushFont(g_heading, 26.f);
            ImGui::TextUnformatted(l.title.c_str());
            ImGui::PopFont();
            ImGui::TextColored(kDim, "by %s  -  %d hearts, %d plays", l.publisher.c_str(), l.hearts, l.plays);
            ImGui::PushTextWrapPos(S(560));
            if (!l.description.empty()) ImGui::TextUnformatted(l.description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            const bool can = a.acct.file.ok() && !c.queue_job.running && !a.shot_mode;
            ImGui::BeginDisabled(!can);
            if (BigButton("Queue this level", ImVec2(S(200), 0), kPlay)) {
                c.queue_job.Start();
                const AccountFile acct = a.acct.file;
                std::thread([&c, server, acct, id = l.id] {
                    std::string token, error;
                    {
                        std::lock_guard lock(c.m);
                        token = c.token;
                    }
                    if (token.empty()) {
                        const WebLogin w = RefreshLogin(server, AccountEmail(acct), acct.password);
                        if (!w.ok) return c.queue_job.Finish(false, "Couldn't sign in to the website: " + w.error);
                        token = w.token;
                        std::lock_guard lock(c.m);
                        c.token = token;  // kept in memory only
                    }
                    const bool ok = QueueLevel(server, token, id, &error);
                    c.queue_job.Finish(ok, ok ? "Queued: it's in your queue in the game (Pod > My Queue)." : "Couldn't queue it: " + error);
                }).detach();
            }
            ImGui::EndDisabled();
            Help("Signs in to the server's website with your active account (once; the sign-in is kept only while the launcher "
                 "is open) and adds the level to your queue, which the game shows in the Pod.");
            ImGui::SameLine();
            if (BigButton("Close", ImVec2(S(120), 0), kCard)) c.selected = -1, ImGui::CloseCurrentPopup();
            std::lock_guard lock(c.queue_job.m);
            if (c.queue_job.running) Spinner(S(9), kCardboard);
            else if (c.queue_job.done) ImGui::TextColored(c.queue_job.ok ? kGood : kBad, "%s", c.queue_job.message.c_str());
            if (!a.acct.file.ok()) Dim("Queueing needs an online account (Online page).");
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    // pages
    ImGui::Spacing();
    ImGui::BeginDisabled(c.skip == 0);
    if (ImGui::Button("< Previous")) c.skip = std::max(0, c.skip - 24), StartLevels(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    Dim("%d-%d of %d", c.skip + 1, c.skip + static_cast<int>(page.levels.size()), page.total);
    ImGui::SameLine();
    ImGui::BeginDisabled(c.skip + 24 >= page.total);
    if (ImGui::Button("Next >")) c.skip += 24, StartLevels(a);
    ImGui::EndDisabled();
}

}  // namespace launcher::ui
