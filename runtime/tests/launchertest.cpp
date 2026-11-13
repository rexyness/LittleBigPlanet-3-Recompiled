// LBP3Launcher's parts, offline: the ini round trip, the settings -> LBP3_* mapping (play_best.bat's
// semantics), online names and server addresses, the worktree -> main checkout lookup, JSON helpers,
// SHA-512, random passwords, CRC32 and the report zip's structure, saves backup/restore, param.sfo
// and the DLC list, the archive check (good and damaged), the log readers (level, play time, a log
// that grows), git log parsing and the updater on a throwaway repository, Discord's IPC against a
// fake pipe of our own (never Discord's), the process runner, shortcuts and images. Everything on
// temporary folders; nothing reaches the network.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "../launcher/launcher_account.h"
#include "../launcher/launcher_discord.h"
#include "../launcher/launcher_dump.h"
#include "../launcher/launcher_proc.h"
#include "../launcher/launcher_settings.h"
#include "../launcher/launcher_shell.h"
#include "../launcher/launcher_stats.h"
#include "../launcher/launcher_sys.h"
#include "../launcher/launcher_update.h"

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


// ---- launcher 2 ----

static void TestSettings2() {
    Settings a;
    a.setup_done = true;
    a.win_x = -1200;
    a.win_y = 40;
    a.win_w = 1400;
    a.win_h = 950;
    a.win_max = true;
    a.discord = true;
    a.discord_app_id = "123456789012345678";
    a.pkg_extractor = "D:\\Tools\\pkg_extractor.exe";
    a.update_remote = "origin";
    const Settings b = FromIni(ToIni(a));
    CHECK(ToIni(a) == ToIni(b));
    CHECK(b.setup_done && b.win_x == -1200 && b.win_y == 40 && b.win_w == 1400 && b.win_h == 950 && b.win_max);
    CHECK(b.discord && b.discord_app_id == "123456789012345678" && b.pkg_extractor == a.pkg_extractor && b.update_remote == "origin");
    const Settings d;
    CHECK(!d.setup_done && !d.discord && d.discord_app_id.empty() && d.update_remote == "fork");  // no ID by default
    CHECK(ValidDiscordAppId("12345678901234567") && ValidDiscordAppId("12345678901234567890"));
    CHECK(!ValidDiscordAppId("") && !ValidDiscordAppId("1234567890123456") && !ValidDiscordAppId("123456789012345678901"));
    CHECK(!ValidDiscordAppId("12345678901234567a") && !ValidDiscordAppId(" 123456789012345678"));
}

static void Put16(std::string& s, size_t at, std::uint16_t v) {
    s[at] = static_cast<char>(v & 0xFF);
    s[at + 1] = static_cast<char>(v >> 8);
}
static void Put32(std::string& s, size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s[at + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
static void PutBe32(std::string& s, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

// A param.sfo with the given text entries and one number.
static std::string MakeSfo(const std::vector<std::pair<std::string, std::string>>& text, const std::string& num_key, std::uint32_t num) {
    struct E {
        std::string key, value;
        bool number;
    };
    std::vector<E> es;
    for (const auto& [k, v] : text) es.push_back({k, v, false});
    es.push_back({num_key, "", true});
    std::string keys, data;
    std::string hdr(20 + 16 * es.size(), '\0');
    hdr.replace(0, 4, std::string("\0PSF", 4));
    Put32(hdr, 4, 0x101);
    for (size_t i = 0; i < es.size(); ++i) {
        const size_t e = 20 + 16 * i;
        Put16(hdr, e, static_cast<std::uint16_t>(keys.size()));
        keys += es[i].key + '\0';
        std::string value;
        if (es[i].number) {
            value.assign(4, '\0');
            Put32(value, 0, num);
        } else {
            value = es[i].value + '\0';
            while (value.size() % 4) value.push_back('\0');
        }
        Put16(hdr, e + 2, es[i].number ? 0x0404 : 0x0204);
        Put32(hdr, e + 4, static_cast<std::uint32_t>(es[i].number ? 4 : es[i].value.size() + 1));
        Put32(hdr, e + 8, static_cast<std::uint32_t>(value.size()));
        Put32(hdr, e + 12, static_cast<std::uint32_t>(data.size()));
        data += value;
    }
    while (keys.size() % 4) keys.push_back('\0');
    Put32(hdr, 8, static_cast<std::uint32_t>(hdr.size()));
    Put32(hdr, 12, static_cast<std::uint32_t>(hdr.size() + keys.size()));
    Put32(hdr, 16, static_cast<std::uint32_t>(es.size()));
    return hdr + keys + data;
}

static void TestSfoAndDlc() {
    const std::string sfo = MakeSfo({{"TITLE", "Back To The Future Costume Pack 2"}, {"TITLE_ID", "CUSA00473"}, {"CONTENT_ID", "UP9000-CUSA00473_00-LBPDLCBTTFCK0002"}}, "ATTRIBUTE", 0x20);
    const Sfo s = ParseSfo(sfo);
    CHECK(s.ok && s.Get("TITLE") == "Back To The Future Costume Pack 2" && s.Get("TITLE_ID") == "CUSA00473");
    CHECK(s.number.count("ATTRIBUTE") && s.number.at("ATTRIBUTE") == 0x20 && s.Get("MISSING").empty());
    CHECK(!ParseSfo("").ok && !ParseSfo("\0PSX" + sfo.substr(4)).ok);
    CHECK(!ParseSfo(sfo.substr(0, sfo.size() - 12)).ok);  // cut short: the last value runs past the end
    // the DLC folder: <title id>\<label>\sce_sys\param.sfo, sorted by name
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_dlc_" + NowStamp());
    auto pack = [&](const char* tid, const char* label, const char* title, bool with_data) {
        const fs::path d = tmp / tid / label;
        fs::create_directories(d / "sce_sys");
        std::ofstream(d / "sce_sys" / "param.sfo", std::ios::binary) << MakeSfo({{"TITLE", title}, {"TITLE_ID", tid}}, "ATTRIBUTE", 0);
        if (with_data) std::ofstream(d / "data.farc", std::ios::binary) << "x";
    };
    pack("CUSA00473", "LBPDLCZZZ0001", "Zebra Costume", false);
    pack("CUSA00473", "LBPDLCAAA0001", "Astronaut Costume", true);
    pack("BCUS98148", "PLITTLEBIG000005", "LittleBigPlanet 1 Content", false);
    // placeholders, as the real license packs have: "1", and one name on several packs
    pack("CUSA00473", "LBPDLC7ELECO0001", "1", false);
    pack("CUSA00473", "LBPDLCBTTFLK0001", "Back To The Future\xE2\x84\xA2 Costume Pack 2", false);
    pack("CUSA00473", "LBPDLCSQEXCK0001", "Back To The Future\xE2\x84\xA2 Costume Pack 2", false);
    fs::create_directories(tmp / "CUSA00473" / "empty_folder");  // no param.sfo: ignored
    const auto packs = ListDlc(tmp);
    CHECK(packs.size() == 6);
    if (packs.size() == 6) {
        CHECK(packs[0].title == "Astronaut Costume" && packs[0].label == "LBPDLCAAA0001" && !packs[0].license_only && !packs[0].placeholder);
        CHECK(packs[1].title == "LittleBigPlanet 1 Content" && packs[1].title_id == "BCUS98148");
        CHECK(packs[2].title == "Zebra Costume" && packs[2].license_only);
        CHECK(packs[3].placeholder && packs[3].title == "LBPDLC7ELECO0001");  // then the placeholders, by label
        CHECK(packs[4].placeholder && packs[4].title == "LBPDLCBTTFLK0001" && packs[5].title == "LBPDLCSQEXCK0001");
    }
    CHECK(ListDlc(tmp / "nope").empty());
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

// A FARC: the files, then count x (sha1, offset, size), count, "FARC" (big-endian).
static std::string MakeFarc(const std::vector<std::string>& files) {
    std::string out, table;
    for (const auto& f : files) {
        table += Sha1Digest(f);
        std::string be;
        PutBe32(be, static_cast<std::uint32_t>(out.size()));
        PutBe32(be, static_cast<std::uint32_t>(f.size()));
        table += be;
        out += f;
    }
    out += table;
    PutBe32(out, static_cast<std::uint32_t>(files.size()));
    return out + "FARC";
}

static void TestFarc() {
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_farc_" + NowStamp());
    fs::create_directories(tmp);
    auto write = [&](const char* name, const std::string& bytes) {
        std::ofstream(tmp / name, std::ios::binary) << bytes;
        return tmp / name;
    };
    std::string big(5000, 'q');
    const std::string good = MakeFarc({"first file", "second, a little longer", big, ""});
    DumpItem r = CheckFarc(write("good.farc", good), 6);
    CHECK(r.level == DumpItem::Ok && r.detail.find("4 files") != std::string::npos && r.detail.find("3 checked") != std::string::npos);
    std::string bad = good;
    bad[2] ^= 0x20;  // a byte inside "first file"
    CHECK(CheckFarc(write("bad.farc", bad), 6).level == DumpItem::Bad);
    // the damaged byte is in a big file that isn't sampled when only one is checked: still OK
    std::string bad_big = good;
    bad_big[40] ^= 1;
    CHECK(CheckFarc(write("bad_big.farc", bad_big), 1).level == DumpItem::Ok);
    CHECK(CheckFarc(write("cut.farc", good.substr(10)), 6).level == DumpItem::Bad);  // offsets point past the data
    CHECK(CheckFarc(write("short.farc", good.substr(0, good.size() - 3)), 6).level == DumpItem::Bad);
    std::string huge_count = good;
    huge_count[huge_count.size() - 8] = 0x7F;  // a table bigger than the file
    CHECK(CheckFarc(write("count.farc", huge_count), 6).level == DumpItem::Bad);
    CHECK(CheckFarc(write("level.farc", std::string(64, '\0') + std::string("\0\0\0\x05" "FAR4", 8)), 6).level == DumpItem::Ok);
    CHECK(CheckFarc(write("text.farc", "hello, not an archive"), 6).level == DumpItem::Bad);
    CHECK(CheckFarc(tmp / "missing.farc", 6).level == DumpItem::Bad);
    // the whole check on a folder without the game: one clear line, no crash
    const auto items = CheckDump(tmp, nullptr, nullptr);
    CHECK(items.size() == 1 && items[0].level == DumpItem::Bad);
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

static void TestLogs() {
    // the level from the game's file opens (real log lines)
    Level lv = LevelFromLogLine("[fs] open /app0/gamedata/lbp3/official_adventure_levels/02_manglewood/04_diner/diner.farc  ->  D:\\x\\diner.farc");
    CHECK(lv.ok() && lv.world == "Manglewood" && lv.level == "Diner" && lv.Text() == "Manglewood - Diner");
    lv = LevelFromLogLine("[fs] open (missing) /temp0/hdd0/CUSA00473_00/gamedata/lbp3/official_adventure_levels/01_prologue/01_needlepoint_peaks/x.farc");
    CHECK(lv.world == "Prologue" && lv.level == "Needlepoint Peaks");
    CHECK(LevelFromLogLine("[fs] open /app0/gamedata/lbp3/official_adventure_levels/02_manglewood/q01_manglewood_bp/a.farc").level == "Manglewood Bp");
    CHECK(!LevelFromLogLine("[video] official_adventure_levels/02_manglewood/04_diner/").ok());
    CHECK(!LevelFromLogLine("[fs] open /app0/gamedata/lbp3/somewhere_else/x.farc").ok());
    CHECK(PrettyName("c01_intro") == "Intro" && PrettyName("big_world") == "Big World" && PrettyName("2015_party") == "2015 Party");
    long flips = 0;
    double avg = 0;
    CHECK(FlipStatusFromLogLine("[video] 43920 frames flipped (avg 36.4 ms, worst 71.4 ms, 6 over 40 ms; 0 new images)", &flips, &avg));
    CHECK(flips == 43920 && avg > 36.3 && avg < 36.5);
    CHECK(!FlipStatusFromLogLine("[video] status lines every 30 flips (LBP3_STATUS_FLIPS)", &flips, &avg));
    CHECK(!FlipStatusFromLogLine("[framegen] 120 frames flipped", &flips, &avg));
    // which logs are play sessions
    CHECK(IsSessionLog("play_best_20261113_0906.txt", true) && IsSessionLog("play_log_20261112_2318.txt", true));
    CHECK(IsSessionLog("play_20261112_2318.txt", true) && IsSessionLog("play_launcher_20261112_2318.2.txt", true));
    CHECK(!IsSessionLog("play_stdout_20261112_2318.txt", true) && !IsSessionLog("play_dlc_20261112_2318.txt", true));
    CHECK(!IsSessionLog("play_best_2026111_0906.txt", true) && !IsSessionLog("notes.txt", true));
    CHECK(IsSessionLog("play_best_log.txt", false) && !IsSessionLog("play_best_stdout.txt", false));

    // the stats over a fake analysis folder
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_stats_" + NowStamp());
    fs::create_directories(tmp / "play_logs");
    auto session = [](long windows, double ms, const char* level) {
        std::string s = "[video] status lines every 30 flips (LBP3_STATUS_FLIPS)\n";
        for (long i = 1; i <= windows; ++i) {
            if (level && i == windows / 2)
                s += std::string("[fs] open /app0/gamedata/lbp3/official_adventure_levels/") + level + "/x.farc\n";
            char line[160];
            std::snprintf(line, sizeof line, "[video] %ld frames flipped (avg %.1f ms, worst 40.0 ms, 0 over 40 ms)\n", i * 30, ms);
            s += line;
        }
        return s;
    };
    // 600 windows of 30 flips at 33.3 ms = 599.4 s; 120 at 50 ms = 180 s; the current one 60 at 40 ms = 72 s
    std::ofstream(tmp / "play_logs" / "play_log_20261101_1200.txt") << session(600, 33.3, "02_manglewood/04_diner");
    std::ofstream(tmp / "play_logs" / "play_best_20261102_1300.txt") << session(120, 50.0, "01_prologue/02_newtons_airship");
    std::ofstream(tmp / "play_logs" / "play_stdout_20261102_1300.txt") << session(999, 33.3, nullptr);  // not a session
    std::ofstream(tmp / "play_logs" / "play_dlc_20261102_1300.txt") << session(999, 33.3, nullptr);     // a test run
    std::ofstream(tmp / "play_best_log.txt") << session(60, 40.0, nullptr);
    PlayStats st = ReadPlayStats(tmp);
    CHECK(st.sessions == 3);
    CHECK(st.seconds >= 850 && st.seconds <= 852);  // 599.4 + 180 + 72
    CHECK(st.last_level == "Prologue - Newtons Airship");  // the newest session with a level
    CHECK(st.last_played > 0);
    // an archived copy of the current log isn't a second session
    fs::copy_file(tmp / "play_best_log.txt", tmp / "play_logs" / "play_best_20261103_0900.txt");
    CHECK(ReadPlayStats(tmp).sessions == 3);
    CHECK(ReadPlayStats(tmp / "nowhere").sessions == 0);

    // following a log as it grows
    const fs::path log = tmp / "tail.txt";
    LogTail tail(log);
    CHECK(tail.Poll().empty());  // not there yet
    std::ofstream(log, std::ios::binary) << "one\r\ntwo\nthr";
    auto lines = tail.Poll();
    CHECK(lines.size() == 2 && lines[0] == "one" && lines[1] == "two");
    std::ofstream(log, std::ios::binary | std::ios::app) << "ee\n";
    lines = tail.Poll();
    CHECK(lines.size() == 1 && lines[0] == "three");
    CHECK(tail.Poll().empty());
    std::ofstream(log, std::ios::binary) << "new\n";  // a new log replaced it (smaller)
    lines = tail.Poll();
    CHECK(lines.size() == 1 && lines[0] == "new");
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

static void TestGitLog() {
    const auto c = ParseLog("abc1234\t2026-11-13\tLauncher: a proper one\r\nbad line\ndef5678\t2026-11-12\tTabs\tin the subject\n");
    CHECK(c.size() == 2);
    if (c.size() == 2) {
        CHECK(c[0].sha == "abc1234" && c[0].when == "2026-11-13" && c[0].subject == "Launcher: a proper one");
        CHECK(c[1].subject == "Tabs\tin the subject");
    }
    CHECK(ParseLog("").empty());
}

// A fake Discord: one client on <prefix>0, answering like the real IPC (or not at all).
enum class FakeDiscord { Normal, Refuse, Silent };
struct FakeDiscordLog {
    std::vector<std::pair<std::uint32_t, std::string>> frames;
};
static bool ReadFrame(HANDLE h, std::uint32_t* op, std::string* json) {
    char hdr[8];
    DWORD n = 0;
    if (!ReadFile(h, hdr, 8, &n, nullptr) || n != 8) return false;
    std::uint32_t len = 0;
    if (!DecodeFrameHeader(std::string(hdr, 8), op, &len)) return false;
    json->assign(len, '\0');
    DWORD got = 0;
    while (got < len) {
        if (!ReadFile(h, json->data() + got, len - got, &n, nullptr) || !n) return false;
        got += n;
    }
    return true;
}
static void WriteFrame(HANDLE h, std::uint32_t op, const std::string& json) {
    const std::string f = EncodeFrame(op, json);
    DWORD n = 0;
    WriteFile(h, f.data(), static_cast<DWORD>(f.size()), &n, nullptr);
}
static void RunFakeDiscord(const std::string& name, FakeDiscord mode, FakeDiscordLog* log, std::atomic<bool>* ready) {
    HANDLE h = CreateNamedPipeA(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
    *ready = true;
    if (h == INVALID_HANDLE_VALUE) return;
    if (ConnectNamedPipe(h, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
        std::uint32_t op = 0;
        std::string json;
        while (ReadFrame(h, &op, &json)) {
            log->frames.emplace_back(op, json);
            if (op == 2) break;
            if (mode == FakeDiscord::Silent) continue;
            if (op == 0) {
                if (mode == FakeDiscord::Refuse) {
                    WriteFrame(h, 2, "{\"code\":4000,\"message\":\"Invalid Client ID\"}");
                    break;
                }
                WriteFrame(h, 1, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}");
            } else if (op == 1) {
                WriteFrame(h, 1, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"evt\":null,\"nonce\":\"" + JsonString(json, "nonce") + "\"}");
            }
        }
        FlushFileBuffers(h);
        DisconnectNamedPipe(h);
    }
    CloseHandle(h);
}

static void TestDiscord() {
    // frames
    const std::string f = EncodeFrame(1, "{\"a\":1}");
    std::uint32_t op = 0, len = 0;
    CHECK(f.size() == 15 && DecodeFrameHeader(f.substr(0, 8), &op, &len) && op == 1 && len == 7 && f.substr(8) == "{\"a\":1}");
    CHECK(!DecodeFrameHeader("short", &op, &len));
    const std::string act = ActivityJson(42, "Playing LittleBigPlanet 3", "Manglewood - \"Diner\"", 1763000000, "n1");
    CHECK(act.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos && act.find("\"pid\":42") != std::string::npos);
    CHECK(act.find("\"state\":\"Manglewood - \\\"Diner\\\"\"") != std::string::npos);
    CHECK(act.find("\"timestamps\":{\"start\":1763000000}") != std::string::npos && act.find("\"nonce\":\"n1\"") != std::string::npos);
    CHECK(ActivityJson(42, "", "", 0, "n2").find("\"activity\":null") != std::string::npos);

    // Only ever our own test pipe, never Discord's (discord-ipc-N).
    const std::string prefix = "\\\\.\\pipe\\lbp3-launchertest-" + std::to_string(GetCurrentProcessId()) + "-";
    SetEnvironmentVariableA("LBP3_LAUNCHER_DISCORD_PIPE", prefix.c_str());
    const std::string app = "123456789012345678";
    {
        DiscordRpc rpc;
        CHECK(!rpc.Connect(app) && rpc.why() == "Discord isn't running");
    }
    auto run = [&](FakeDiscord mode, FakeDiscordLog* log, const std::function<void(DiscordRpc&)>& body) {
        std::atomic<bool> ready{false};
        std::thread server(RunFakeDiscord, prefix + "0", mode, log, &ready);
        while (!ready) Sleep(5);
        {
            DiscordRpc rpc;
            body(rpc);
        }
        server.join();
    };
    FakeDiscordLog normal;
    run(FakeDiscord::Normal, &normal, [&](DiscordRpc& rpc) {
        CHECK(rpc.Connect(app) && rpc.Connected());
        CHECK(rpc.SetActivity("Playing LittleBigPlanet 3", "Manglewood - Diner", 1763000000));
        rpc.Clear();
        CHECK(rpc.Connected());
    });
    CHECK(normal.frames.size() == 4);
    if (normal.frames.size() == 4) {
        CHECK(normal.frames[0].first == 0 && normal.frames[0].second == "{\"v\":1,\"client_id\":\"123456789012345678\"}");
        CHECK(normal.frames[1].first == 1 && normal.frames[1].second.find("\"details\":\"Playing LittleBigPlanet 3\"") != std::string::npos);
        CHECK(normal.frames[1].second.find("\"start\":1763000000") != std::string::npos);
        CHECK(normal.frames[2].first == 1 && normal.frames[2].second.find("\"activity\":null") != std::string::npos);
        CHECK(normal.frames[3].first == 2);  // closed politely
    }
    FakeDiscordLog refused;
    run(FakeDiscord::Refuse, &refused, [&](DiscordRpc& rpc) {
        CHECK(!rpc.Connect(app) && !rpc.Connected());
        CHECK(rpc.why().find("Invalid Client ID") != std::string::npos);
    });
    FakeDiscordLog silent;
    const auto t0 = GetTickCount64();
    run(FakeDiscord::Silent, &silent, [&](DiscordRpc& rpc) {
        CHECK(!rpc.Connect(app) && rpc.why() == "Discord didn't answer");  // gives up instead of hanging
    });
    CHECK(GetTickCount64() - t0 < 8000);
    SetEnvironmentVariableA("LBP3_LAUNCHER_DISCORD_PIPE", nullptr);
}

static void TestProc() {
    CHECK(Quote(L"plain") == L"plain" && Quote(L"with space") == L"\"with space\"" && Quote(L"") == L"\"\"");
    CHECK(Quote(L"C:\\a b\\") == L"\"C:\\a b\\\\\"");  // a trailing backslash doubled before the quote
    CHECK(Quote(L"say \"hi\"") == L"\"say \\\"hi\\\"\"");
    CHECK(!FindOnPath(L"cmd.exe").empty() && FindOnPath(L"no-such-program-here.exe").empty());
    std::vector<std::string> lines;
    const fs::path cwd = fs::temp_directory_path();
    ProcResult r = RunCapture(L"cmd.exe /d /c \"echo hello& echo world& exit /b 3\"", cwd, [&](const std::string& l) { lines.push_back(l); }, nullptr, 20000);
    CHECK(r.started && r.exit_code == 3);
    CHECK(lines.size() >= 2 && lines[0].rfind("hello", 0) == 0 && lines[1].rfind("world", 0) == 0);
    CHECK(r.output.find("hello") != std::string::npos);
    const auto t0 = GetTickCount64();
    r = RunCapture(L"cmd.exe /d /c \"ping -n 30 127.0.0.1 >nul\"", cwd, nullptr, nullptr, 500);
    CHECK(r.error == "timed out" && GetTickCount64() - t0 < 10000);
    std::atomic<bool> cancel{true};
    r = RunCapture(L"cmd.exe /d /c \"ping -n 30 127.0.0.1 >nul\"", cwd, nullptr, &cancel, 0);
    CHECK(r.error == "cancelled");
    r = RunCapture(L"no-such-program-here.exe", cwd, nullptr, nullptr, 2000);
    CHECK(!r.started && !r.error.empty());
}

// The updater on a throwaway repository with a local "remote" and a stub build.bat. Never a real
// worktree or the network.
static ProcResult G(const fs::path& dir, const std::wstring& args) {
    return RunCapture(Quote(FindOnPath(L"git.exe").wstring()) + L" -c user.name=launchertest -c user.email=launchertest@example.invalid "
                          L"-c init.defaultBranch=main -c core.autocrlf=false -C " + Quote(dir.wstring()) + L" " + args,
                      dir, nullptr, nullptr, 60000);
}
static void TestUpdater() {
    if (FindOnPath(L"git.exe").empty()) {
        std::printf("  (git not found: updater test skipped)\n");
        return;
    }
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_update_" + NowStamp());
    const fs::path remote = tmp / "remote.git", work = tmp / "work", other = tmp / "other";
    fs::create_directories(remote);
    fs::create_directories(work);
    CHECK(G(remote, L"init --bare").exit_code == 0);
    CHECK(G(work, L"init").exit_code == 0);
    std::ofstream(work / "build.bat") << "@echo off\r\necho stub build ran> built.txt\r\necho building %*\r\nexit /b 0\r\n";
    std::ofstream(work / ".gitignore") << "built.txt\n";
    std::ofstream(work / "readme.txt", std::ios::binary) << "one\n";
    G(work, L"add -A");
    CHECK(G(work, L"commit -m \"First\"").exit_code == 0);
    G(work, L"remote add origin " + Quote(remote.wstring()));
    CHECK(G(work, L"push -u origin main").exit_code == 0);
    CHECK(G(tmp, L"clone " + Quote(remote.wstring()) + L" other").exit_code == 0);
    std::ofstream(other / "readme.txt", std::ios::binary) << "two\n";
    G(other, L"commit -am \"Second: the new thing\"");
    CHECK(G(other, L"push origin main").exit_code == 0);

    std::vector<std::string> log;
    auto keep = [&](const std::string& l) { log.push_back(l); };
    UpdateInfo u = CheckUpdates(work, "origin", keep, nullptr);
    CHECK(u.ok && u.branch == "main" && !u.dirty && u.incoming.size() == 1);
    if (u.incoming.size() == 1) CHECK(u.incoming[0].subject == "Second: the new thing");
    CHECK(ReadFile(work / "readme.txt") == "one\n");  // checking only looks
    // refused while the game runs, before anything changes
    std::string error;
    CHECK(!UpdateAndBuild(work, "origin", false, [] { return true; }, keep, nullptr, &error));
    CHECK(error.find("running") != std::string::npos && ReadFile(work / "readme.txt") == "one\n");
    // refused with local changes
    std::ofstream(work / "readme.txt", std::ios::binary) << "edited here\n";
    CHECK(!UpdateAndBuild(work, "origin", false, [] { return false; }, keep, nullptr, &error));
    CHECK(error.find("local changes") != std::string::npos && ReadFile(work / "readme.txt") == "edited here\n");
    G(work, L"checkout -- readme.txt");
    // the update and the (stub) build
    log.clear();
    CHECK(UpdateAndBuild(work, "origin", true, [] { return false; }, keep, nullptr, &error));
    // (the pull checks out with the user's own git settings, so the line end may be \r\n)
    CHECK(ReadFile(work / "readme.txt").rfind("two", 0) == 0 && fs::exists(work / "built.txt"));
    bool target = false;
    for (const auto& l : log) target |= l.find("building --target lbp3") != std::string::npos;
    CHECK(target);  // game_only builds just the game
    u = CheckUpdates(work, "origin", nullptr, nullptr);
    CHECK(u.ok && u.incoming.empty());
    const auto recent = RecentChanges(work, 5);
    CHECK(recent.size() == 2 && recent[0].subject == "Second: the new thing");
    // not a checkout
    CHECK(!CheckUpdates(tmp, "origin", nullptr, nullptr).ok);
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(tmp, ec)) fs::permissions(e.path(), fs::perms::owner_write, fs::perm_options::add, ec);
    fs::remove_all(tmp, ec);
}

static void TestShell() {
    // a shortcut, in a temporary folder (never the real Desktop or Start menu)
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_shell_" + NowStamp());
    fs::create_directories(tmp);
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::string error;
    CHECK(CreateShortcutAt(tmp / "LittleBigPlanet 3 Launcher.lnk", self, &error));
    std::error_code ec;
    CHECK(fs::file_size(tmp / "LittleBigPlanet 3 Launcher.lnk", ec) > 100);
    CHECK(ShortcutPath(ShortcutPlace::Desktop).filename() == "LittleBigPlanet 3 Launcher.lnk");
    // images: our own icon decodes; a missing file doesn't
    const Image icon = LoadImageFile(fs::path(__FILE__).parent_path().parent_path() / "launcher" / "lbp3launcher_256.png");
    CHECK(icon.ok() && icon.w == 256 && icon.h == 256 && icon.rgba.size() == 256u * 256u * 4u);
    CHECK(!LoadImageFile(tmp / "missing.png").ok());
    Image four;
    four.w = 2;
    four.h = 2;
    four.rgba = {0, 0, 0, 255, 100, 100, 100, 255, 200, 200, 200, 255, 100, 100, 100, 255};
    const Image one = Downscale(four, 1, 1);
    CHECK(one.w == 1 && one.h == 1 && one.rgba[0] == 100 && one.rgba[3] == 255);
    CHECK(Downscale(four, 8, 8).w == 2);  // never enlarged
    const Image wide = Downscale(icon, 64, 32);
    CHECK(wide.w == 32 && wide.h == 32);
    fs::remove_all(tmp, ec);
}

static void TestOnlineSwitch() {
    // One switch: Refresh (LBP3_ONLINE*) and shadNet (LBP3_SHADNET*) together; never a password.
    Settings s;
    s.online = true;
    s.online_server = "play.lbppc.com";
    s.online_name = "Sack_boy-1";
    auto m = EnvMap(s);
    CHECK(m["LBP3_ONLINE"] == "1" && m["LBP3_ONLINE_SERVER"] == "https://play.lbppc.com" && m["LBP3_ONLINE_NAME"] == "Sack_boy-1");
    CHECK(m["LBP3_SHADNET"] == "1" && m["LBP3_SHADNET_SERVER"] == "play.lbppc.com");
    CHECK(!m.count("LBP3_SHADNET_PASSWORD") && !m.count("LBP3_SHADNET_NAME"));
    s.online_server = "127.0.0.1:10061";
    m = EnvMap(s);
    CHECK(m["LBP3_ONLINE_SERVER"] == "http://127.0.0.1:10061" && m["LBP3_SHADNET_SERVER"] == "127.0.0.1");
    s.shadnet_server = " tls://sn.example.org:31313/ ";
    CHECK(EnvMap(s)["LBP3_SHADNET_SERVER"] == "tls://sn.example.org:31313");
    s.online = false;
    m = EnvMap(s);
    CHECK(!m.count("LBP3_ONLINE") && !m.count("LBP3_SHADNET") && !m.count("LBP3_SHADNET_SERVER"));
    s.online = true;
    s.online_name = "";
    CHECK(!EnvMap(s).count("LBP3_SHADNET"));  // no name: all of online stays off
    Settings r;
    r.shadnet_server = "tls://sn.example.org:31313";
    CHECK(FromIni(ToIni(r)).shadnet_server == r.shadnet_server);

    // the account file, in the tool's format and the old launcher's (comments, several blocks)
    AccountFile a = ParseAccountFile("name=Sack_boy-1\npassword=abc-DEF_123\nserver=play.lbppc.com\n");
    CHECK(a.ok() && a.name == "Sack_boy-1" && a.password == "abc-DEF_123" && a.server == "play.lbppc.com");
    a = ParseAccountFile("# 2026-11-12 10:00\r\nserver=http://127.0.0.1:10061\r\nname=Old_one\r\npassword=x1\r\n\r\n# later\r\nname: Newer\r\nPassword = p=with=equals \r\n");
    CHECK(a.name == "Newer" && a.password == "p=with=equals" && a.server == "http://127.0.0.1:10061");
    CHECK(!ParseAccountFile("name=\npassword=\n").ok() && !ParseAccountFile("").ok());

    // EffectiveOnline: an empty name or server comes from the file; typed ones win
    const fs::path tmp = fs::temp_directory_path() / ("launchertest_online_" + NowStamp());
    Paths p;
    p.root = tmp;
    fs::create_directories(tmp / "user" / "config");
    Settings e;
    e.online = true;
    CHECK(EffectiveOnline(e, p).online_name.empty());  // no file yet
    std::ofstream(tmp / "user" / "config" / "online_account.txt", std::ios::binary) << "name=Sack_boy-1\npassword=secret\nserver=play.lbppc.com\n";
    CHECK(AccountFilePath(p) == tmp / "user" / "config" / "online_account.txt");
    Settings f = EffectiveOnline(e, p);
    CHECK(f.online_name == "Sack_boy-1" && f.online_server == "play.lbppc.com");
    e.online_name = "Typed_name";
    e.online_server = "127.0.0.1:10061";
    f = EffectiveOnline(e, p);
    CHECK(f.online_name == "Typed_name" && f.online_server == "127.0.0.1:10061");
    e.online_name.clear();
    e.online_server.clear();
    std::map<std::string, std::string> env;
    for (const auto& [k, v] : GameEnvironment(e, p, {})) env[k] = v;
    CHECK(env["LBP3_ONLINE_NAME"] == "Sack_boy-1" && env["LBP3_SHADNET"] == "1");
    bool password_anywhere = false;
    for (const auto& [k, v] : env) password_anywhere |= v.find("secret") != std::string::npos;
    CHECK(!password_anywhere);  // the game reads the password from the file itself
    CHECK(IsSessionLog("play_shadnet_20261113_1000.txt", true) && IsSessionLog("play_shadnet_log.txt", false));
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

// Only with LAUNCHERTEST_DUMP=<the folder that holds CUSA00473>: the file check on the real dump
// (read-only), every line printed. A good dump must have no "X" line.
static void TestRealDump() {
    char dir[512];
    const DWORD n = GetEnvironmentVariableA("LAUNCHERTEST_DUMP", dir, sizeof dir);
    if (!n || n >= sizeof dir) return;
    const auto t0 = GetTickCount64();
    const auto items = CheckDump(dir, nullptr, nullptr);
    int bad = 0;
    for (const auto& it : items) {
        std::printf("  %s %s  %s\n", it.level == DumpItem::Ok ? "OK" : it.level == DumpItem::Warn ? "! " : "X ", it.what.c_str(), it.detail.c_str());
        bad += it.level == DumpItem::Bad;
    }
    std::printf("  (%d lines, %.1f s)\n", static_cast<int>(items.size()), (GetTickCount64() - t0) / 1000.0);
    CHECK(items.size() > 10 && bad == 0);
    const auto packs = ListDlc(fs::path(dir) / "CUSA00473-DLC");
    std::printf("  %d DLC packs; first: %s\n", static_cast<int>(packs.size()), packs.empty() ? "-" : packs[0].title.c_str());
}

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    TestAccountAgainstMock();
    TestIni();
    TestMapping();
    TestOnlineText();
    TestPaths();
    TestZip();
    TestSaves();
    TestSettings2();
    TestSfoAndDlc();
    TestFarc();
    TestLogs();
    TestGitLog();
    TestDiscord();
    TestProc();
    TestUpdater();
    TestShell();
    TestOnlineSwitch();
    TestRealDump();
    std::printf("launchertest: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
