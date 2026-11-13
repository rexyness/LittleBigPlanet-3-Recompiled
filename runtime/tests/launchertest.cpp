// LBP3Launcher's pure parts, offline: the ini round trip, the settings -> LBP3_* mapping (play_best.bat's
// semantics), online names and server addresses, the worktree -> main checkout lookup, JSON helpers,
// SHA-512, random passwords, CRC32 and the report zip's structure, and saves backup/restore on a
// temporary folder.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "../launcher/launcher_account.h"
#include "../launcher/launcher_settings.h"
#include "../launcher/launcher_sys.h"

using namespace launcher;
namespace fs = std::filesystem;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failed;                                                          \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #cond);              \
        }                                                                        \
    } while (0)

static std::map<std::string, std::string> EnvMap(const Settings& s) {
    std::map<std::string, std::string> m;
    for (const auto& [k, v] : MapSettings(s).env) m[k] = v;
    return m;
}

static void TestIni() {
    Settings a;
    a.build = Build::Best;
    a.exe_path = "C:\\x\\lbp3.exe";
    a.game_root = "D:\\Recomps\\lbp3-recompiled";
    a.cache = CacheChoice::Shared;
    a.minimize_while_playing = false;
    a.skip_intro = false;
    a.framegen = false;
    a.res = 1080;
    a.my_unlocks = true;
    a.bloom_1440 = false;
    a.fg_text = false;
    a.status_flips = 300;
    a.pipeline_threads = 4;
    a.vk_pipeline_cache = false;
    a.menu = false;
    a.stats = true;
    a.quit_menu = false;
    a.language = "pt-br";
    a.motion_filter = false;
    a.tilt_keys = false;
    a.mouse_touch = true;
    a.online = true;
    a.online_server = "play.example.org";
    a.online_name = "Sack_boy-1";
    const Settings b = FromIni(ToIni(a));
    CHECK(ToIni(a) == ToIni(b));
    CHECK(b.build == Build::Best && b.cache == CacheChoice::Shared && b.res == 1080 && b.pipeline_threads == 4);
    CHECK(b.language == "pt-br" && b.online_name == "Sack_boy-1" && b.game_root == a.game_root);
    // defaults survive an empty or broken file
    const Settings d = FromIni("");
    CHECK(ToIni(d) == ToIni(Settings{}));
    const Settings e = FromIni("[game]\nres=999\nframegen=maybe\nstatus_flips=-3\n[launcher]\nbuild=weird\nnot a line\n");
    CHECK(e.res == 1440 && e.framegen && e.status_flips == 30 && e.build == Build::This);
    // values with line breaks are flattened on write
    Settings f;
    f.online_name = "a\nb";
    CHECK(ToIni(f).find("name=a b\n") != std::string::npos);
}

static void TestMapping() {
    // the defaults: as play_best.bat's defaults
    auto m = EnvMap(Settings{});
    CHECK(m["LBP3_FRAMEGEN"] == "1" && m["LBP3_FRAMEGEN_MODE"] == "warp");
    CHECK(m["LBP3_RES"] == "1440" && m["LBP3_RES_SHADERS"] == "1" && !m.count("LBP3_RES_SKIP"));
    CHECK(m["LBP3_STATUS_FLIPS"] == "30");
    CHECK(!m.count("LBP3_MY_UNLOCKS") && !m.count("LBP3_FRAMEGEN_TEXT") && !m.count("LBP3_PIPELINE_THREADS"));
    CHECK(!m.count("LBP3_VK_PIPELINE_CACHE") && !m.count("LBP3_MENU") && !m.count("LBP3_MOTION"));
    CHECK(!m.count("LBP3_TILT_KEYS") && !m.count("LBP3_MOUSE_TOUCH") && !m.count("LBP3_ONLINE"));
    CHECK(!m.count("LBP3_STATS") && !m.count("LBP3_LANGUAGE") && !m.count("LBP3_LANGUAGES"));
    const LaunchPlan plan = MapSettings(Settings{});
    CHECK(plan.args.size() == 2 && plan.args[0] == "--run" && plan.args[1] == "--skip-intro");
    // every switch flipped
    Settings s;
    s.skip_intro = false;
    s.framegen = false;
    s.bloom_1440 = false;
    s.my_unlocks = true;
    s.fg_text = false;
    s.status_flips = 300;
    s.pipeline_threads = 1;
    s.vk_pipeline_cache = false;
    s.menu = false;
    s.motion_filter = false;
    s.tilt_keys = false;
    s.mouse_touch = true;
    s.stats = true;
    s.language = "de";
    m = EnvMap(s);
    CHECK(!m.count("LBP3_FRAMEGEN") && !m.count("LBP3_FRAMEGEN_MODE"));
    CHECK(m["LBP3_RES_SKIP"] == "bloom");
    CHECK(m["LBP3_MY_UNLOCKS"] == "lbp1,lbp2");
    CHECK(m["LBP3_FRAMEGEN_TEXT"] == "off");
    CHECK(m["LBP3_STATUS_FLIPS"] == "300");
    CHECK(m["LBP3_PIPELINE_THREADS"] == "1");
    CHECK(m["LBP3_VK_PIPELINE_CACHE"] == "off" && m["LBP3_MENU"] == "off");
    CHECK(m["LBP3_MOTION"] == "raw" && m["LBP3_TILT_KEYS"] == "off" && m["LBP3_MOUSE_TOUCH"] == "1");
    CHECK(m["LBP3_STATS"] == "1" && m["LBP3_LANGUAGE"] == "de" && m["LBP3_LANGUAGES"] == "all");
    CHECK(MapSettings(s).args.size() == 1);
    // 1080: no resolution variables at all, bloom irrelevant
    Settings r;
    r.res = 1080;
    r.bloom_1440 = false;
    m = EnvMap(r);
    CHECK(!m.count("LBP3_RES") && !m.count("LBP3_RES_SHADERS") && !m.count("LBP3_RES_SKIP"));
    // online only with a valid server and name
    Settings o;
    o.online = true;
    o.online_server = "play.example.org";
    o.online_name = "ab";
    CHECK(!EnvMap(o).count("LBP3_ONLINE"));
    o.online_name = "Player_1";
    m = EnvMap(o);
    CHECK(m["LBP3_ONLINE"] == "1" && m["LBP3_ONLINE_SERVER"] == "https://play.example.org" && m["LBP3_ONLINE_NAME"] == "Player_1");
    o.online_server = "not a server";
    CHECK(!EnvMap(o).count("LBP3_ONLINE"));
    o.online_server = "127.0.0.1:10061";
    o.online = false;
    CHECK(!EnvMap(o).count("LBP3_ONLINE"));
}

static void TestOnlineText() {
    CHECK(ValidOnlineName("abc") && ValidOnlineName("A_b-9xyzABCDEFGH") && !ValidOnlineName("ab"));
    CHECK(!ValidOnlineName("seventeen_chars_x") && !ValidOnlineName("sp ace") && !ValidOnlineName("dot.name"));
    CHECK(NormalizeServer("play.lbppc.com") == "https://play.lbppc.com");
    CHECK(NormalizeServer(" https://play.lbppc.com/ ") == "https://play.lbppc.com");
    CHECK(NormalizeServer("127.0.0.1:10061") == "http://127.0.0.1:10061");
    CHECK(NormalizeServer("localhost") == "http://localhost");
    CHECK(NormalizeServer("myhost:8080") == "http://myhost:8080");
    CHECK(NormalizeServer("http://10.0.0.5:10061") == "http://10.0.0.5:10061");
    CHECK(NormalizeServer("ftp://x").empty() && NormalizeServer("").empty());
    CHECK(NormalizeServer("host/path").empty() && NormalizeServer("host:99999").empty() && NormalizeServer("a b").empty());
    CHECK(NormalizeServer("user@host").empty());
    // JSON
    const std::string reply = R"({"success": true, "data": {"tokenData": "abc\"def\\x", "other": 1}})";
    CHECK(JsonTrue(reply, "success"));
    CHECK(JsonString(reply, "tokenData") == "abc\"def\\x");
    CHECK(JsonString(reply, "missing").empty() && !JsonTrue(reply, "data"));
    CHECK(!JsonTrue(R"({"success":false,"error":"that online name is taken"})", "success"));
    CHECK(JsonString(R"({"success":false,"error":"that online name is taken"})", "error") == "that online name is taken");
    CHECK(JsonEscape("a\"b\\c\n") == "a\\\"b\\\\c\\n");
    // SHA-512 of "abc" (FIPS 180-2)
    CHECK(Sha512Hex("abc") ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd"
          "454d4423643ce80e2a9ac94fa54ca49f");
    const std::string p1 = RandomPassword(), p2 = RandomPassword();
    CHECK(p1.size() == 20 && p2.size() == 20 && p1 != p2);
    CHECK(p1.find_first_not_of("ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789") == std::string::npos);
}

static void TestPaths() {
    CHECK(MainCheckoutFromGitFile("gitdir: D:/Recomps/lbp3-recompiled/.git/worktrees/launcher\n") ==
          fs::path("D:\\Recomps\\lbp3-recompiled"));
    CHECK(MainCheckoutFromGitFile("gitdir: C:\\a b\\repo\\.git\\worktrees\\x") == fs::path("C:\\a b\\repo"));
    CHECK(MainCheckoutFromGitFile("gitdir: ../x/.git").empty() && MainCheckoutFromGitFile("hello").empty());
    // FindRoot on a fake layout: <tmp>\root (has game_dir.txt), <tmp>\wt\build (a worktree of it)
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_" + NowStamp());
    fs::create_directories(tmp / "root" / "build");
    fs::create_directories(tmp / "wt" / "build");
    std::ofstream(tmp / "root" / "game_dir.txt") << "C:\\Games\\PS4\r\n";
    std::ofstream(tmp / "wt" / ".git") << "gitdir: " << (tmp / "root" / ".git" / "worktrees" / "wt").generic_string() << "\n";
    CHECK(FindRoot(tmp / "root" / "build", "") == tmp / "root");
    CHECK(FindRoot(tmp / "wt" / "build", "") == tmp / "root");
    CHECK(FindRoot(tmp / "wt" / "build", "E:\\elsewhere") == fs::path("E:\\elsewhere"));
    CHECK(FindRoot(tmp, "").empty());
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

static void TestZip() {
    CHECK(Crc32("123456789", 9) == 0xCBF43926u);
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_zip_" + NowStamp() + ".zip");
    std::string error;
    CHECK(WriteZip(tmp, {{"a.txt", "hello"}, {"dir/b.txt", std::string(1000, 'x')}}, &error));
    const std::string z = ReadFile(tmp);
    auto u32 = [&](size_t at) {
        return static_cast<uint32_t>(static_cast<unsigned char>(z[at])) | static_cast<uint32_t>(static_cast<unsigned char>(z[at + 1])) << 8 |
               static_cast<uint32_t>(static_cast<unsigned char>(z[at + 2])) << 16 | static_cast<uint32_t>(static_cast<unsigned char>(z[at + 3])) << 24;
    };
    CHECK(z.size() > 30 && u32(0) == 0x04034b50);
    CHECK(u32(14) == Crc32("hello", 5) && u32(18) == 5 && u32(22) == 5);
    CHECK(z.compare(30, 5, "a.txt") == 0 && z.compare(35, 5, "hello") == 0);
    // end of central directory: 2 entries, the directory's offset points at a central header
    const size_t eocd = z.size() - 22;
    CHECK(u32(eocd) == 0x06054b50);
    const uint32_t cd = u32(eocd + 16);
    CHECK(cd < z.size() && u32(cd) == 0x02014b50);
    CHECK(static_cast<unsigned char>(z[eocd + 10]) == 2);
    std::error_code ec;
    fs::remove(tmp, ec);
}

static void TestSaves() {
    // Backup and restore on a fake game folder (never the real one).
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_saves_" + NowStamp());
    Paths p;
    p.root = tmp;
    p.saves = tmp / "user" / "savedata" / "CUSA00473";
    p.backups = tmp / "analysis" / "savedata_backups";
    fs::create_directories(p.saves / "LBPxSAVE" / "00000001");
    std::ofstream(p.saves / "LBPxSAVE" / "00000001" / "bigfart1") << "version one";
    std::string error;
    const std::string first = BackupSaves(p, "manual", &error);
    CHECK(!first.empty() && fs::exists(p.backups / first / "LBPxSAVE" / "00000001" / "bigfart1"));
    std::ofstream(p.saves / "LBPxSAVE" / "00000001" / "bigfart1") << "version two!";
    auto list = ListBackups(p);
    CHECK(list.size() == 1 && list[0].name == first);
    std::string safety;
    CHECK(RestoreBackup(p, list[0], &error, &safety));
    CHECK(ReadFile(p.saves / "LBPxSAVE" / "00000001" / "bigfart1") == "version one");
    CHECK(!safety.empty() && ReadFile(p.backups / safety / "LBPxSAVE" / "00000001" / "bigfart1") == "version two!");
    CHECK(ListBackups(p).size() == 2);
    // no leftovers next to the saves
    int extra = 0;
    for (const auto& e : fs::directory_iterator(p.saves.parent_path())) extra += e.path().filename() != "CUSA00473";
    CHECK(extra == 0);
    // a missing backup fails without touching the saves
    Backup gone{"gone", tmp / "nope", 0};
    CHECK(!RestoreBackup(p, gone, &error, &safety));
    CHECK(ReadFile(p.saves / "LBPxSAVE" / "00000001" / "bigfart1") == "version one");
    // log rotation and the report
    p.log_base = "play_best";
    fs::create_directories(tmp / "analysis");
    std::ofstream(tmp / "analysis" / "play_best_log.txt") << "session log";
    RotateLogs(p);
    CHECK(fs::exists(tmp / "analysis" / "play_best_log.1.txt") && !fs::exists(tmp / "analysis" / "play_best_log.txt"));
    int kept = 0;
    for (const auto& e : fs::directory_iterator(tmp / "analysis" / "play_logs")) kept += e.path().filename().string().rfind("play_best_", 0) == 0;
    CHECK(kept == 1);
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

// Only with LAUNCHERTEST_MOCK=http://127.0.0.1:<port> (tools/mock_lbp_server.py running): the real
// Create account calls against the localhost mock. Never a real server: anything not 127.0.0.1 is
// refused here.
static void TestAccountAgainstMock() {
    const char* mock = std::getenv("LAUNCHERTEST_MOCK");
    if (!mock || !*mock) return;
    const std::string server = NormalizeServer(mock);
    if (server.rfind("http://127.0.0.1:", 0) != 0) {
        std::printf("  LAUNCHERTEST_MOCK must be http://127.0.0.1:<port>; skipped\n");
        return;
    }
    const std::string name = "lt_" + NowStamp().substr(9);  // a fresh name per run
    const AccountResult a = CreateRefreshAccount(server, name);
    std::printf("  mock account: %s\n", a.message.c_str());
    CHECK(a.ok && a.password.size() == 20 && a.message.find("created") != std::string::npos);
    const AccountResult b = CreateRefreshAccount(server, name);  // the same name again
    CHECK(!b.ok && b.message.find("taken") != std::string::npos);
    const AccountResult c = CreateRefreshAccount("http://127.0.0.1:1", "nobody_here");
    CHECK(!c.ok && c.message.find("Couldn't reach") != std::string::npos);
}

int main() {
    TestAccountAgainstMock();
    TestIni();
    TestMapping();
    TestOnlineText();
    TestPaths();
    TestZip();
    TestSaves();
    std::printf("launchertest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
