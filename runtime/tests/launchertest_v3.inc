// Launcher 3's logic, included by launchertest.cpp (its CHECK and helpers). Everything offline, on
// temporary folders; the shadNet tests talk to a fake server on 127.0.0.1 only.

static std::string Slurp3(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void TestV3Settings() {
    Settings a;
    a.display = "165hz";
    a.window_mode = "borderless";
    a.volume = 40;
    a.downmix = false;
    a.dlc_dir = "E:\\LBP3 DLC";
    a.update_on_start = true;
    a.tray_while_playing = true;
    a.tour_done = true;
    a.confirm_quit = false;
    const Settings b = FromIni(ToIni(a));
    CHECK(ToIni(a) == ToIni(b));
    CHECK(b.display == "165hz" && b.window_mode == "borderless" && b.volume == 40 && !b.downmix && b.dlc_dir == a.dlc_dir);
    CHECK(b.update_on_start && b.tray_while_playing && b.tour_done && !b.confirm_quit);
    auto m = EnvMap(a);
    CHECK(m["LBP3_DISPLAY"] == "165hz" && m["LBP3_WINDOW"] == "borderless" && m["LBP3_VOLUME"] == "40" && m["LBP3_DOWNMIX"] == "sdl");
    const Settings d;
    m = EnvMap(d);  // the defaults set none of them
    CHECK(!m.count("LBP3_DISPLAY") && !m.count("LBP3_WINDOW") && !m.count("LBP3_VOLUME") && !m.count("LBP3_DOWNMIX"));
    CHECK(d.window_mode == "windowed" && d.volume == 100 && d.downmix && !d.update_on_start && d.confirm_quit);
    CHECK(ValidDisplay("1") && ValidDisplay("16") && ValidDisplay("165hz") && ValidDisplay("59.94Hz"));
    CHECK(!ValidDisplay("") && !ValidDisplay("0") && !ValidDisplay("17") && !ValidDisplay("abc") && !ValidDisplay("1.5") && !ValidDisplay("hz"));
    CHECK(ValidWindowMode("fullscreen") && !ValidWindowMode("maximized"));
    // bad values in the ini keep the defaults
    const Settings e = FromIni("[game]\nwindow_mode=huge\nvolume=150\ndisplay=x\n");
    CHECK(e.window_mode == "windowed" && e.volume == 100 && e.display.empty());
    // the DLC folder setting reaches the game as LBP3_ADDCONT_DIR, and only when set
    Paths p;
    p.root = fs::temp_directory_path();
    std::map<std::string, std::string> env;
    for (const auto& [k, v] : GameEnvironment(a, p, {})) env[k] = v;
    CHECK(env["LBP3_ADDCONT_DIR"] == "E:\\LBP3 DLC");
    env.clear();
    for (const auto& [k, v] : GameEnvironment(d, p, {})) env[k] = v;
    CHECK(!env.count("LBP3_ADDCONT_DIR"));
}

static void TestAccountsV3() {
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_accts_" + NowStamp());
    const fs::path cfg = tmp / "user" / "config";
    fs::create_directories(cfg);
    std::ofstream(cfg / "online_account.txt", std::ios::binary) << "name=Kisto_Test\npassword=first-pass-1\nserver=play.lbppc.com\n";
    std::ofstream(cfg / "online_account_grinch.txt", std::ios::binary) << "# a comment\nname=Grinch_Test\npassword=wrong-pass-9\n";
    std::ofstream(cfg / "online_account.txt.bak", std::ios::binary) << "name=Old\npassword=x\n";  // not an account
    auto list = ListAccounts(cfg);
    CHECK(list.size() == 2);
    if (list.size() == 2) {
        CHECK(list[0].active && list[0].data.name == "Kisto_Test" && !list[1].active && list[1].data.name == "Grinch_Test");
    }
    // the active file is the single source of the online name and server
    Paths p;
    p.root = tmp;
    Settings typed;
    typed.online = true;
    typed.online_name = "Someone_Else";
    typed.online_server = "127.0.0.1:10061";
    Settings eff = EffectiveOnline(typed, p);
    CHECK(eff.online_name == "Kisto_Test" && eff.online_server == "play.lbppc.com");
    // switching renames; nothing is lost or overwritten
    std::string error;
    CHECK(SetActiveAccount(cfg, cfg / "online_account_grinch.txt", &error));
    list = ListAccounts(cfg);
    CHECK(list.size() == 2 && list[0].active && list[0].data.name == "Grinch_Test");
    CHECK(fs::exists(cfg / "online_account_kisto_test.txt") && Slurp3(cfg / "online_account_kisto_test.txt").find("first-pass-1") != std::string::npos);
    CHECK(EffectiveOnline(typed, p).online_name == "Grinch_Test");
    CHECK(SetActiveAccount(cfg, cfg / "online_account.txt", &error));  // already active: nothing to do
    CHECK(!SetActiveAccount(cfg, cfg / "nope.txt", &error));
    // a new password: every password line, other lines kept, the old text as .bak
    CHECK(SetAccountPassword(cfg / "online_account.txt", "fixed-pass-7", &error));
    const std::string now = Slurp3(cfg / "online_account.txt");
    CHECK(now.find("password=fixed-pass-7") != std::string::npos && now.find("wrong-pass-9") == std::string::npos);
    CHECK(now.find("# a comment") != std::string::npos && now.find("name=Grinch_Test") != std::string::npos);
    CHECK(Slurp3(cfg / "online_account.txt.bak").find("wrong-pass-9") != std::string::npos);
    CHECK(!SetAccountPassword(cfg / "online_account.txt", "short", &error));
    CHECK(!SetAccountPassword(cfg / "online_account.txt", " spaced-password ", &error));
    CHECK(ValidAccountPassword("abcdefgh") && !ValidAccountPassword("abc\ndefgh"));
    // new account files never overwrite
    CHECK(NewAccountFile(cfg, "Kisto_Test") == cfg / "online_account_kisto_test_2.txt");
    AccountFile n;
    n.name = "New_One";
    n.password = "new-pass-123";
    n.server = "play.lbppc.com";
    n.email = AccountEmail(n);
    CHECK(n.email == "new_one@lbppc.invalid");
    CHECK(WriteAccountFile(cfg / "online_account_new.txt", n, &error));
    CHECK(!WriteAccountFile(cfg / "online_account_new.txt", n, &error));
    CHECK(ParseAccountFile(Slurp3(cfg / "online_account_new.txt")).email == "new_one@lbppc.invalid");
    const fs::path empty = tmp / "empty";
    fs::create_directories(empty);
    CHECK(NewAccountFile(empty, "Any") == empty / "online_account.txt");
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

static void TestRefreshParse() {
    ServerInfo info;
    CHECK(ParseInstance(R"({"success":true,"data":{"instanceName":"LBPPC","instanceDescription":"A server","softwareName":"Refresh",
        "softwareVersion":"2.5","registrationEnabled":true,"maintenanceModeEnabled":false}})", &info));
    CHECK(info.name == "LBPPC" && info.software == "Refresh" && info.version == "2.5" && info.registration && !info.maintenance);
    CHECK(!ParseInstance(R"({"success":false,"error":{"message":"x"}})", &info) && !ParseInstance("not json", &info));
    std::string error;
    const auto ann = ParseAnnouncements(R"({"success":true,"data":[{"announcementId":"1","title":"Hello","text":"Welcome %s %n",
        "createdAt":"2026-11-13T10:00:00+00:00"}]})", &error);
    CHECK(ann.size() == 1 && ann[0].title == "Hello" && ann[0].text == "Welcome %s %n" && ann[0].created == "2026-11-13");
    CHECK(ParseAnnouncements("{}", &error).empty() && !error.empty());
    const LevelPage page = ParseLevels(R"({"success":true,"data":[
        {"levelId":12,"title":"Sky Race","description":"fast","iconHash":"0123456789abcdef0123456789abcdef01234567",
         "publisher":{"username":"Maker_1"},"hearts":40,"uniquePlays":300,"yayRatings":7,"teamPicked":true},
        {"levelId":13,"title":"","iconHash":"g12345","publisher":null,"originalPublisher":"Old_Maker","hearts":0}],
        "listInfo":{"nextPageIndex":3,"totalItems":57}})");
    CHECK(page.error.empty() && page.levels.size() == 2 && page.total == 57);
    if (page.levels.size() == 2) {
        CHECK(page.levels[0].id == 12 && page.levels[0].title == "Sky Race" && page.levels[0].publisher == "Maker_1");
        CHECK(page.levels[0].hearts == 40 && page.levels[0].plays == 300 && page.levels[0].team_picked);
        CHECK(page.levels[1].title == "Unnamed level" && page.levels[1].publisher == "Old_Maker");
        CHECK(IsSha1Hex(page.levels[0].icon_hash) && !IsSha1Hex(page.levels[1].icon_hash));
    }
    CHECK(!ParseLevels(R"({"success":false})").error.empty());
    CHECK(LevelsPath("newest", "", 0, 20) == "/levels/newest?skip=1&count=20");  // the server's skip counts from 1
    CHECK(LevelsPath("search", "big & fun", 20, 10) == "/levels/search?skip=21&count=10&query=big%20%26%20fun");
    CHECK(UrlEncode("a/b c\xC3\xA9") == "a%2Fb%20c%C3%A9");
    CHECK(ApiBase("https://play.lbppc.com/") == "https://play.lbppc.com/api/v3");
    CHECK(ExplainApiError(403, R"({"success":false,"error":{"name":"ApiAuthenticationError","message":"The email or password was incorrect."}})", "")
              .find("e-mail or password") != std::string::npos);
    CHECK(ExplainApiError(429, "", "").find("too many") != std::string::npos);
    CHECK(ExplainApiError(0, "", "timeout").find("didn't answer") != std::string::npos);
    CHECK(ExplainApiError(500, "", "").find("HTTP 500") != std::string::npos);
    CHECK(ExplainApiError(400, R"({"error":{"message":"Bad thing"}})", "") == "Bad thing");
}

static void TestFriendsModel() {
    ShadnetEndpoint e = ParseShadnetServer("play.lbppc.com");
    CHECK(e.host == "shadnet.lbppc.com" && e.port == 31313 && e.tls);
    CHECK(ParseShadnetServer("https://play.lbppc.com/").tls);
    e = ParseShadnetServer("tls://sn.example.org:4000");
    CHECK(e.host == "sn.example.org" && e.port == 4000 && e.tls);
    e = ParseShadnetServer("127.0.0.1");
    CHECK(e.host == "127.0.0.1" && e.port == 31313 && !e.tls);
    e = ParseShadnetServer("http://127.0.0.1:10061");
    CHECK(e.host == "127.0.0.1" && e.port == 10061);
    CHECK(!ParseShadnetServer("").ok() && !ParseShadnetServer("host:99999").ok());

    FriendsList l;
    l.friends = {{"Zed_9", false}, {"amy_1", true}, {"Bob_2", false}};
    l.received = {"Stranger_9"};
    l.sent = {"Waiting_3"};
    SortFriends(l);
    CHECK(l.friends[0].name == "amy_1" && l.friends[1].name == "Bob_2" && l.friends[2].name == "Zed_9");
    AddFriendDone(l, "Stranger_9");  // they asked: an accept
    CHECK(l.received.empty() && l.friends.size() == 4);
    AddFriendDone(l, "New_Pal");  // a request
    CHECK(l.sent.size() == 2 && l.friends.size() == 4);
    FriendAdded(l, "new_pal", true);  // they accepted (names compare without case)
    CHECK(l.sent.size() == 1 && l.friends.size() == 5);
    FriendCameOnline(l, "Bob_2", true);
    SortFriends(l);
    CHECK(l.friends[0].online && l.friends[1].online && l.friends[2].online);
    RequestReceived(l, "Fan_5");
    RequestReceived(l, "Fan_5");
    CHECK(l.received.size() == 1);
    RemoveFriendDone(l, "Fan_5");  // declined
    RemoveFriendDone(l, "Waiting_3");  // cancelled
    RemoveFriendDone(l, "Zed_9");  // removed
    CHECK(l.received.empty() && l.sent.empty() && l.friends.size() == 4);
    FriendLost(l, "amy_1");
    CHECK(l.friends.size() == 3);
    CHECK(ExplainShadnetState(8).find("wrong password") != std::string::npos);
    CHECK(ExplainShadnetState(6).find("signed in already") != std::string::npos);
    CHECK(ExplainFriendError(26).find("no player") != std::string::npos);
}

static void TestReleaseLogic() {
    Version v = ParseVersion("v0.1.0-lbp3port");
    CHECK(v.ok && v.major == 0 && v.minor == 1 && v.patch == 0 && v.Text() == "0.1.0");
    CHECK(ParseVersion("1.12.3").ok && ParseVersion("V2.0.10").patch == 10);
    CHECK(!ParseVersion("v1.2").ok && !ParseVersion("latest").ok && !ParseVersion("").ok);
    CHECK(CompareVersions(ParseVersion("v0.1.0"), ParseVersion("v0.2.0")) < 0);
    CHECK(CompareVersions(ParseVersion("v1.10.0"), ParseVersion("v1.9.9")) > 0);
    CHECK(CompareVersions(ParseVersion("v0.1.0-a"), ParseVersion("v0.1.0-b")) == 0);
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_release_" + NowStamp());
    fs::create_directories(tmp / "install");
    CHECK(InstalledVersion(tmp / "install").empty());
    std::ofstream(tmp / "install" / "SOURCE.md") << "# Source\n\nBuilt from bestofboth (Visual Studio 2026 with clang-cl). Package version: v0.1.0-lbp3port.\n";
    CHECK(InstalledVersion(tmp / "install") == "v0.1.0-lbp3port");

    ReleaseInfo r = ParseLatestRelease(R"({"tag_name":"v0.2.0-lbp3port","name":"LBP3 Recompiled 0.2.0","body":"Notes",
        "published_at":"2026-11-20T12:00:00Z","assets":[
        {"name":"LBP3-Recompiled-v0.2.0-lbp3port.zip","browser_download_url":"https://example.invalid/a.zip","size":1234},
        {"name":"LBP3-Recompiled-v0.2.0-lbp3port.zip.sha256","browser_download_url":"https://example.invalid/a.zip.sha256"},
        {"name":"other.txt","browser_download_url":"https://example.invalid/o"}]})");
    CHECK(r.ok && r.tag == "v0.2.0-lbp3port" && r.zip_url == "https://example.invalid/a.zip" && r.zip_size == 1234);
    CHECK(r.sha_url == "https://example.invalid/a.zip.sha256" && r.published == "2026-11-20" && r.notes == "Notes");
    CHECK(!ParseLatestRelease(R"({"tag_name":"v1.0.0","assets":[]})").ok);
    CHECK(ParseLatestRelease(R"({"message":"Not Found"})").error.find("Not Found") != std::string::npos);
    CHECK(ParseSha256Text("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD  LBP3.zip\n") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(ParseSha256Text("no hash here").empty());
    std::ofstream(tmp / "abc.txt", std::ios::binary) << "abc";
    CHECK(Sha256File(tmp / "abc.txt") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // A release zip (one top folder), made with miniz, unpacked and installed over a fake install.
    const fs::path zip = tmp / "release.zip";
    {
        mz_zip_archive a{};
        CHECK(mz_zip_writer_init_file(&a, zip.string().c_str(), 0));
        auto add = [&](const char* name, const std::string& data) {
            mz_zip_writer_add_mem(&a, name, data.data(), data.size(), MZ_BEST_SPEED);
        };
        add("LBP3-Recompiled-v0.2.0/LBP3Launcher.exe", "new launcher");
        add("LBP3-Recompiled-v0.2.0/lbp3.exe", "new game");
        add("LBP3-Recompiled-v0.2.0/SDL3.dll", "new sdl");
        add("LBP3-Recompiled-v0.2.0/tools/readme.txt", "new tool");
        add("LBP3-Recompiled-v0.2.0/user/config/launcher.ini", "MUST NOT LAND");
        add("LBP3-Recompiled-v0.2.0/analysis/x.txt", "MUST NOT LAND");
        mz_zip_writer_finalize_archive(&a);
        mz_zip_writer_end(&a);
    }
    std::string error;
    CHECK(ExtractZip(zip, tmp / "unpacked", &error));
    const fs::path inst = tmp / "install";
    fs::create_directories(inst / "user" / "config");
    fs::create_directories(inst / "analysis");
    std::ofstream(inst / "LBP3Launcher.exe", std::ios::binary) << "old launcher";
    std::ofstream(inst / "SDL3.dll", std::ios::binary) << "old sdl";
    std::ofstream(inst / "user" / "config" / "launcher.ini", std::ios::binary) << "mine";
    std::ofstream(inst / "analysis" / "x.txt", std::ios::binary) << "my log";
    std::ofstream(inst / "notes.old", std::ios::binary) << "the player's own .old file";
    // SDL3.dll "in use": open without write sharing (as a loaded DLL is); it can be renamed, not overwritten.
    HANDLE inUse = CreateFileW((inst / "SDL3.dll").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    std::vector<std::string> changed;
    CHECK(ApplyUpdate(tmp / "unpacked", inst, &changed, &error));
    if (inUse != INVALID_HANDLE_VALUE) CloseHandle(inUse);
    CHECK(Slurp3(inst / "LBP3Launcher.exe") == "new launcher" && Slurp3(inst / "lbp3.exe") == "new game");
    CHECK(Slurp3(inst / "SDL3.dll") == "new sdl" && Slurp3(inst / "SDL3.dll.old") == "old sdl");
    CHECK(Slurp3(inst / "tools" / "readme.txt") == "new tool");
    CHECK(Slurp3(inst / "user" / "config" / "launcher.ini") == "mine" && Slurp3(inst / "analysis" / "x.txt") == "my log");
    CHECK(changed.size() == 4);
    RemoveOldFiles(inst);
    CHECK(!fs::exists(inst / "SDL3.dll.old") && fs::exists(inst / "notes.old"));  // only ours
    CHECK(ProtectedPath("user/config/a.txt") && ProtectedPath("Analysis/x") && ProtectedPath("logs") && !ProtectedPath("tools/x"));
    // a release without the programs isn't installed
    fs::create_directories(tmp / "empty_release" / "docs");
    std::ofstream(tmp / "empty_release" / "docs" / "a.md") << "x";
    CHECK(!ApplyUpdate(tmp / "empty_release", inst, nullptr, &error));
    // unsafe zip paths are refused
    const fs::path evil = tmp / "evil.zip";
    {
        mz_zip_archive a{};
        mz_zip_writer_init_file(&a, evil.string().c_str(), 0);
        mz_zip_writer_add_mem(&a, "ok.txt", "x", 1, MZ_BEST_SPEED);
        const bool added = mz_zip_writer_add_mem(&a, "../escape.txt", "x", 1, MZ_BEST_SPEED);
        mz_zip_writer_finalize_archive(&a);
        mz_zip_writer_end(&a);
        if (added) {
            CHECK(!ExtractZip(evil, tmp / "evil_out", &error) && !fs::exists(tmp / "escape.txt"));
        }
    }
    CHECK(!ExtractZip(tmp / "abc.txt", tmp / "x_out", &error));
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

static void TestInputConfig() {
    InputConfig c = InputConfig::FromText("# my keys\njump = Space\nunknown_thing = 7\njump = Up\n\n# end note\n");
    CHECK(c.Get("jump") == "Up" && c.Get("grab") == "Left Shift" && c.Get("pad_cross") == "south");  // last wins, defaults
    c.Set("grab", "Left Ctrl");
    c.Set("pad_circle", "south");
    c.Set("pad_cross", "east");
    CHECK(c.Changed());
    std::string text = c.Text();
    CHECK(text.find("# my keys") != std::string::npos && text.find("unknown_thing = 7") != std::string::npos);
    CHECK(text.find("# end note") != std::string::npos && text.find("jump = Up") != std::string::npos);
    CHECK(text.find("jump = Space") == std::string::npos);  // the duplicate line is gone
    CHECK(text.find("grab = Left Ctrl") != std::string::npos && text.find("pad_cross = east") != std::string::npos);
    CHECK(text.find("pad_square") == std::string::npos);  // untouched pad buttons stay unwritten
    InputConfig back = InputConfig::FromText(text);
    CHECK(back.Get("grab") == "Left Ctrl" && back.Get("pad_circle") == "south" && back.Get("jump") == "Up");
    c.Set("confirm", "Space, Return");
    c.Set("jump", "Space");
    auto clashes = c.Clashes();
    CHECK(clashes.count("Space") && clashes["Space"].size() == 2);
    c.ResetAll();
    CHECK(c.Get("pad_cross") == "south" && c.Get("jump") == "Space" && c.Clashes().empty());
    CHECK(SplitKeys(" Escape , Backspace,") == std::vector<std::string>({"Escape", "Backspace"}));
    // a fresh file: the runtime's header, every action, no pad lines
    const std::string fresh = InputConfig::FromText("").Text();
    CHECK(fresh.find("# Keyboard controls.") == 0 && fresh.find("tilt_back = Keypad 2") != std::string::npos);
    CHECK(fresh.find("pad_") == std::string::npos);
    // saving keeps the old file as .bak
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_input_" + NowStamp());
    fs::create_directories(tmp);
    std::ofstream(tmp / "input.ini", std::ios::binary) << "jump = Space\n";
    InputConfig s = InputConfig::Load(tmp / "input.ini");
    s.Set("jump", "Up");
    std::string error;
    CHECK(s.Save(tmp / "input.ini", &error) && !s.Changed());
    CHECK(Slurp3(tmp / "input.ini.bak") == "jump = Space\n" && InputConfig::Load(tmp / "input.ini").Get("jump") == "Up");
    CHECK(ControlRows().size() == std::size(input::actions::kActionRows) + std::size(input::actions::kPadRows));
    CHECK(input::actions::PadButtonFromName("south") == SDL_GAMEPAD_BUTTON_SOUTH &&
          input::actions::PadButtonFromName("x") == SDL_GAMEPAD_BUTTON_INVALID);
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

// Only with LAUNCHERTEST_DUMP: the native Quit Game writer on the real dump (read-only), and, when
// Python is there, the same edit by tools/quit_menu.py: the edited scripts must unpack to the
// same bytes (the zlib streams themselves may differ).
struct EditsFile {
    bool ok = false;
    std::string farcAdd, mapPatch;
    std::uint64_t farcCut = 0;
};
static EditsFile ReadEdits(const std::string& d) {
    EditsFile e;
    size_t at = 12;
    auto le = [&](size_t n) {
        std::uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= std::uint64_t(std::uint8_t(d[at + i])) << (8 * i);
        at += n;
        return v;
    };
    if (d.size() < 12 || d.compare(0, 8, "LBPEDIT1") != 0) return e;
    for (int sec = 0; sec < 2; ++sec) {
        const size_t nameLen = size_t(le(2));
        const std::string name = d.substr(at, nameLen);
        at += nameLen;
        le(8);
        const std::uint64_t cut = le(8), addLen = le(8);
        const std::string add = d.substr(at, size_t(addLen));
        at += size_t(addLen);
        const std::uint32_t patches = std::uint32_t(le(4));
        std::string patch;
        for (std::uint32_t i = 0; i < patches; ++i) {
            le(8);
            const size_t n = size_t(le(4));
            patch += d.substr(at, n);
            at += n;
        }
        if (name == "patch_001.farc") e.farcAdd = add, e.farcCut = cut;
        else e.mapPatch = patch;
    }
    e.ok = !e.farcAdd.empty() && e.mapPatch.size() == 24;
    return e;
}
static std::string ScriptBody(const EditsFile& e) {
    const std::uint32_t size = std::uint32_t(std::uint8_t(e.mapPatch[0])) << 24 | std::uint32_t(std::uint8_t(e.mapPatch[1])) << 16 |
                               std::uint32_t(std::uint8_t(e.mapPatch[2])) << 8 | std::uint8_t(e.mapPatch[3]);
    const std::string res = e.farcAdd.substr(0, size);
    auto be16 = [&](size_t at) { return size_t(std::uint8_t(res[at]) << 8 | std::uint8_t(res[at + 1])); };
    const size_t count = be16(20);
    size_t data = 22 + 4 * count;
    std::string body;
    for (size_t i = 0; i < count; ++i) {
        const size_t packed = be16(22 + 4 * i), unpacked = be16(24 + 4 * i);
        std::string chunk(unpacked, '\0');
        mz_ulong len = mz_ulong(unpacked);
        mz_uncompress(reinterpret_cast<unsigned char*>(chunk.data()), &len, reinterpret_cast<const unsigned char*>(res.data() + data), mz_ulong(packed));
        body += chunk.substr(0, len);
        data += packed;
    }
    return body;
}
static void TestQuitMenuReal() {
    char dir[512];
    const DWORD n = GetEnvironmentVariableA("LAUNCHERTEST_DUMP", dir, sizeof dir);
    if (!n || n >= sizeof dir) return;
    const fs::path folder = QuitMenuFolder(dir);
    CHECK(!folder.empty());
    if (folder.empty()) return;
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_quit_" + NowStamp());
    fs::create_directories(tmp / "py");
    const QuitMenuResult r = WriteQuitMenuEdits(folder, tmp / "file_edits.bin");
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok && fs::exists(tmp / "file_edits.bin"));
    const EditsFile ours = ReadEdits(Slurp3(tmp / "file_edits.bin"));
    CHECK(ours.ok);
    // the new script's hash is in the map patch and in the archive's table
    if (ours.ok) {
        const std::string sha = ours.mapPatch.substr(4, 20);
        const std::uint32_t size = std::uint32_t(ours.farcAdd.size());  // checked below against Python's
        CHECK(Sha1Digest(ours.farcAdd.substr(0, ours.farcAdd.size() - 8 - 28 * (std::uint32_t(std::uint8_t(ours.farcAdd[size - 8])) << 24 |
                                                                                 std::uint32_t(std::uint8_t(ours.farcAdd[size - 7])) << 16 |
                                                                                 std::uint32_t(std::uint8_t(ours.farcAdd[size - 6])) << 8 |
                                                                                 std::uint8_t(ours.farcAdd[size - 5])))) == sha);
        CHECK(ours.farcAdd.find(sha) != std::string::npos && ours.farcAdd.compare(ours.farcAdd.size() - 4, 4, "FARC") == 0);
    }
    const fs::path python = PythonPath();
    const fs::path script = fs::path(__FILE__).parent_path().parent_path().parent_path() / "tools" / "quit_menu.py";
    if (!python.empty() && fs::exists(script)) {
        const ProcResult py = RunCapture(Quote(python.wstring()) + L" -I " + Quote(script.wstring()) + L" " + Quote(folder.wstring()) +
                                             L" --out " + Quote((tmp / "py").wstring()),
                                         tmp, nullptr, nullptr, 120000);
        const EditsFile theirs = ReadEdits(Slurp3(tmp / "py" / "file_edits.bin"));
        CHECK(py.started && theirs.ok);
        if (ours.ok && theirs.ok) {
            const std::string a = ScriptBody(ours), b = ScriptBody(theirs);
            std::printf("  the edited script: %zu bytes, %s Python's\n", a.size(), a == b ? "the same as" : "DIFFERENT from");
            CHECK(!a.empty() && a == b);
            CHECK(ours.farcCut == theirs.farcCut);
        }
    } else {
        std::printf("  (Python or tools/quit_menu.py not found: compared with nothing)\n");
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
}
