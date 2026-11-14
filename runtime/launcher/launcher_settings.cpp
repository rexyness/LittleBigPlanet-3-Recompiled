// LBP3Launcher's settings (launcher_settings.h).
#include "launcher_settings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace launcher {
namespace {

std::string Trim(std::string s) {
    while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ') s.pop_back();
    size_t i = 0;
    while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ') ++i;
    return s.substr(i);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ParseBool(const std::string& v, bool& out) {
    const std::string l = Lower(v);
    if (l == "1" || l == "on" || l == "yes" || l == "true") return out = true, true;
    if (l == "0" || l == "off" || l == "no" || l == "false") return out = false, true;
    return false;
}

bool ParseInt(const std::string& v, int lo, int hi, int& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (*end || n < lo || n > hi) return false;
    out = static_cast<int>(n);
    return true;
}

// Values go on one line: no line breaks, no leading or trailing blanks.
std::string OneLine(std::string v) {
    for (char& c : v)
        if (c == '\r' || c == '\n') c = ' ';
    return Trim(v);
}

}  // namespace

const char* BuildKey(Build b) {
    switch (b) {
    case Build::Best: return "best";
    case Build::Port: return "port";
    case Build::Custom: return "custom";
    default: return "this";
    }
}

const char* CacheKey(CacheChoice c) {
    switch (c) {
    case CacheChoice::Own: return "own";
    case CacheChoice::Shared: return "shared";
    default: return "auto";
    }
}

std::string ToIni(const Settings& s) {
    std::ostringstream o;
    auto b = [](bool v) { return v ? "1" : "0"; };
    o << "; LBP3Launcher settings (the launcher writes this file; editing it by hand is fine).\n"
         "; Each switch is one of play_best.bat's; 1 = on, 0 = off.\n"
         "[launcher]\n"
         "; which lbp3.exe: this (next to the launcher), best (lbp3-wt\\best), port (the game folder's), custom\n"
      << "build=" << BuildKey(s.build) << "\n"
      << "exe_path=" << OneLine(s.exe_path) << "\n"
      << "; the folder with user\\ and game_dir.txt; empty = found automatically\n"
      << "game_root=" << OneLine(s.game_root) << "\n"
      << "; shader cache: auto (own copy for builds other than the game folder's), own, shared\n"
      << "shader_cache=" << CacheKey(s.cache) << "\n"
      << "minimize_while_playing=" << b(s.minimize_while_playing) << "\n"
      << "; the format of this file (the launcher keeps it up to date)\n"
      << "settings_version=" << kSettingsVersion << "\n"
      << "[game]\n"
      << "skip_intro=" << b(s.skip_intro) << "\n"
      << "framegen=" << b(s.framegen) << "\n"
      << "; render size: 1280x720, 1920x1080, 2560x1440, 3840x2160, 2560x1080, 3440x1440, 3840x1600, 3840x1080, 4480x1260\n"
      << "res=" << s.res << "\n"
      << "my_unlocks=" << b(s.my_unlocks) << "\n"
      << "fg_text=" << b(s.fg_text) << "\n"
      << "status_flips=" << s.status_flips << "\n"
      << "; 0 = all CPU cores but one\n"
      << "pipeline_threads=" << s.pipeline_threads << "\n"
      << "vk_pipeline_cache=" << b(s.vk_pipeline_cache) << "\n"
      << "; 1 = new shaders are built in the background: no freeze, but new things can appear a moment late\n"
      << "async_shaders=" << b(s.async_shaders) << "\n"
      << "menu=" << b(s.menu) << "\n"
      << "stats=" << b(s.stats) << "\n"
      << "quit_menu=" << b(s.quit_menu) << "\n"
      << "; empty = English; a code such as de, fr, ja, pt-br\n"
      << "language=" << OneLine(s.language) << "\n"
      << "motion_filter=" << b(s.motion_filter) << "\n"
      << "tilt_keys=" << b(s.tilt_keys) << "\n"
      << "mouse_touch=" << b(s.mouse_touch) << "\n"
      << "; display: empty, a number (1 = the first display) or a refresh rate such as 165hz\n"
      << "display=" << OneLine(s.display) << "\n"
      << "; windowed, borderless or fullscreen\n"
      << "window_mode=" << OneLine(s.window_mode) << "\n"
      << "volume=" << s.volume << "\n"
      << "downmix=" << b(s.downmix) << "\n"
      << "; empty = the dump's CUSA00473-DLC\n"
      << "dlc_dir=" << OneLine(s.dlc_dir) << "\n"
      << "[online]\n"
      << "; off unless 1, and only with a server and a name\n"
      << "online=" << b(s.online) << "\n"
      << "server=" << OneLine(s.online_server) << "\n"
      << "name=" << OneLine(s.online_name) << "\n"
      << "; empty = the server's own shadNet\n"
      << "shadnet_server=" << OneLine(s.shadnet_server) << "\n"
      << "[window]\n"
      << "x=" << s.win_x << "\ny=" << s.win_y << "\nw=" << s.win_w << "\nh=" << s.win_h << "\n"
      << "maximized=" << b(s.win_max) << "\n"
      << "setup_done=" << b(s.setup_done) << "\n"
      << "[discord]\n"
      << "; Rich Presence (off unless 1); app_id is your own Discord application's ID\n"
      << "enabled=" << b(s.discord) << "\n"
      << "app_id=" << OneLine(s.discord_app_id) << "\n"
      << "[tools]\n"
      << "; empty = pkg_extractor.exe in the game dump's folder\n"
      << "pkg_extractor=" << OneLine(s.pkg_extractor) << "\n"
      << "update_remote=" << OneLine(s.update_remote) << "\n"
      << "update_on_start=" << b(s.update_on_start) << "\n"
      << "tray_while_playing=" << b(s.tray_while_playing) << "\n"
      << "tour_done=" << b(s.tour_done) << "\n"
      << "confirm_quit=" << b(s.confirm_quit) << "\n";
    return o.str();
}

Settings FromIni(const std::string& text) {
    Settings s;
    int version = 1;  // files without settings_version are from v0.1.3 or older
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Lower(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Lower(Trim(line.substr(0, eq)));
        const std::string v = Trim(line.substr(eq + 1));
        int n = 0;
        if (section == "launcher") {
            if (key == "build") {
                const std::string l = Lower(v);
                if (l == "this") s.build = Build::This;
                else if (l == "best") s.build = Build::Best;
                else if (l == "port") s.build = Build::Port;
                else if (l == "custom") s.build = Build::Custom;
            } else if (key == "exe_path") s.exe_path = v;
            else if (key == "game_root") s.game_root = v;
            else if (key == "shader_cache") {
                const std::string l = Lower(v);
                if (l == "auto") s.cache = CacheChoice::Auto;
                else if (l == "own") s.cache = CacheChoice::Own;
                else if (l == "shared") s.cache = CacheChoice::Shared;
            } else if (key == "minimize_while_playing") ParseBool(v, s.minimize_while_playing);
            else if (key == "settings_version" && ParseInt(v, 1, 1000, n)) version = n;
        } else if (section == "game") {
            if (key == "skip_intro") ParseBool(v, s.skip_intro);
            else if (key == "framegen") ParseBool(v, s.framegen);
            else if (key == "res") {
                if (const std::string r = CanonicalRes(v); !r.empty()) s.res = r;
            } else if (key == "my_unlocks") ParseBool(v, s.my_unlocks);
            // bloom_1440 (older files) is ignored: the capture chain keeps its 1080p sizes at 16:9 now.
            else if (key == "fg_text") ParseBool(v, s.fg_text);
            else if (key == "status_flips") {
                if (ParseInt(v, 1, 100000, n)) s.status_flips = n;
            } else if (key == "pipeline_threads") {
                if (v.empty()) s.pipeline_threads = 0;
                else if (ParseInt(v, 0, 256, n)) s.pipeline_threads = n;
            } else if (key == "vk_pipeline_cache") ParseBool(v, s.vk_pipeline_cache);
            else if (key == "async_shaders") ParseBool(v, s.async_shaders);
            else if (key == "menu") ParseBool(v, s.menu);
            else if (key == "stats") ParseBool(v, s.stats);
            else if (key == "quit_menu") ParseBool(v, s.quit_menu);
            else if (key == "language") s.language = Lower(v);
            else if (key == "motion_filter") ParseBool(v, s.motion_filter);
            else if (key == "tilt_keys") ParseBool(v, s.tilt_keys);
            else if (key == "mouse_touch") ParseBool(v, s.mouse_touch);
            else if (key == "display" && ValidDisplay(v)) s.display = v;
            else if (key == "window_mode" && ValidWindowMode(v)) s.window_mode = v;
            else if (key == "volume" && ParseInt(v, 0, 100, n)) s.volume = n;
            else if (key == "downmix") ParseBool(v, s.downmix);
            else if (key == "dlc_dir") s.dlc_dir = v;
        } else if (section == "online") {
            if (key == "online") ParseBool(v, s.online);
            else if (key == "server") s.online_server = v;
            else if (key == "name") s.online_name = v;
            else if (key == "shadnet_server") s.shadnet_server = v;
        } else if (section == "window") {
            if (key == "x" && ParseInt(v, -100000, 100000, n)) s.win_x = n;
            else if (key == "y" && ParseInt(v, -100000, 100000, n)) s.win_y = n;
            else if (key == "w" && ParseInt(v, 0, 20000, n)) s.win_w = n;
            else if (key == "h" && ParseInt(v, 0, 20000, n)) s.win_h = n;
            else if (key == "maximized") ParseBool(v, s.win_max);
            else if (key == "setup_done") ParseBool(v, s.setup_done);
        } else if (section == "discord") {
            if (key == "enabled") ParseBool(v, s.discord);
            else if (key == "app_id") s.discord_app_id = v;
        } else if (section == "tools") {
            if (key == "pkg_extractor") s.pkg_extractor = v;
            else if (key == "update_remote" && !v.empty()) s.update_remote = v;
            else if (key == "update_on_start") ParseBool(v, s.update_on_start);
            else if (key == "tray_while_playing") ParseBool(v, s.tray_while_playing);
            else if (key == "tour_done") ParseBool(v, s.tour_done);
            else if (key == "confirm_quit") ParseBool(v, s.confirm_quit);
        }
    }
    // Version 2: the check on launch is on by default. Every older file says update_on_start=0
    // (the launcher writes every key, and the default was off), which isn't a choice: switch it on
    // once. From then on the file has settings_version=2, so turning it off sticks.
    if (version < 2) s.update_on_start = true;
    return s;
}

std::string ShadnetServer(const Settings& s) {
    std::string typed = s.shadnet_server;
    while (!typed.empty() && (typed.back() == ' ' || typed.back() == '/')) typed.pop_back();
    while (!typed.empty() && typed.front() == ' ') typed.erase(typed.begin());
    if (!typed.empty()) return typed;
    std::string host = NormalizeServer(s.online_server);
    if (const size_t scheme = host.find("://"); scheme != std::string::npos) host = host.substr(scheme + 3);
    if (const size_t slash = host.find('/'); slash != std::string::npos) host.resize(slash);
    if (const size_t colon = host.rfind(':'); colon != std::string::npos) host.resize(colon);  // shadNet has its own port
    return host;
}

bool ValidWindowMode(const std::string& m) { return m == "windowed" || m == "borderless" || m == "fullscreen"; }

bool ValidDisplay(const std::string& d) {
    if (d.empty() || d.size() > 12) return false;
    size_t i = 0, digits = 0;
    bool dot = false;
    for (; i < d.size() && (std::isdigit(static_cast<unsigned char>(d[i])) || (d[i] == '.' && !dot)); ++i) {
        if (d[i] == '.') dot = true;
        else ++digits;
    }
    if (!digits) return false;
    const std::string rest = d.substr(i);
    if (rest.empty()) {
        if (dot) return false;
        const int n = std::atoi(d.c_str());
        return n >= 1 && n <= 16;
    }
    return rest == "hz" || rest == "Hz" || rest == "HZ";
}

bool ValidDiscordAppId(const std::string& id) {
    return id.size() >= 17 && id.size() <= 20 &&
           std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

LaunchPlan MapSettings(const Settings& s) {
    LaunchPlan p;
    p.args.push_back("--run");
    if (s.skip_intro) p.args.push_back("--skip-intro");
    auto set = [&](const char* k, const std::string& v) { p.env.emplace_back(k, v); };
    if (s.framegen) {
        set("LBP3_FRAMEGEN", "1");
        set("LBP3_FRAMEGEN_MODE", "warp");
    }
    if (const std::string r = CanonicalRes(s.res); !r.empty() && r != "1920x1080") {
        set("LBP3_RES", r == "2560x1440" ? "1440" : r);  // "1440": the original switch, the same size
        set("LBP3_RES_SHADERS", "1");
    }
    if (s.my_unlocks) set("LBP3_MY_UNLOCKS", "lbp1,lbp2");
    if (!s.fg_text) set("LBP3_FRAMEGEN_TEXT", "off");
    set("LBP3_STATUS_FLIPS", std::to_string(s.status_flips));
    if (s.pipeline_threads > 0) set("LBP3_PIPELINE_THREADS", std::to_string(s.pipeline_threads));
    if (!s.vk_pipeline_cache) set("LBP3_VK_PIPELINE_CACHE", "off");
    if (s.async_shaders) set("LBP3_ASYNC_PIPELINES", "1");
    if (!s.menu) set("LBP3_MENU", "off");
    if (!s.motion_filter) set("LBP3_MOTION", "raw");
    if (!s.tilt_keys) set("LBP3_TILT_KEYS", "off");
    if (s.mouse_touch) set("LBP3_MOUSE_TOUCH", "1");
    if (!s.display.empty() && ValidDisplay(s.display)) set("LBP3_DISPLAY", s.display);
    if (s.window_mode == "borderless" || s.window_mode == "fullscreen") set("LBP3_WINDOW", s.window_mode);
    if (s.volume >= 0 && s.volume < 100) set("LBP3_VOLUME", std::to_string(s.volume));
    if (!s.downmix) set("LBP3_DOWNMIX", "sdl");
    if (s.online) {
        const std::string server = NormalizeServer(s.online_server);
        // Online needs both: without them the game would sign in to nothing.
        if (!server.empty() && ValidOnlineName(s.online_name)) {
            set("LBP3_ONLINE", "1");
            set("LBP3_ONLINE_SERVER", server);
            set("LBP3_ONLINE_NAME", s.online_name);
            // shadNet: sign-in, friends and multiplayer (LBPPC's server needs it for online play).
            // Builds without shadNet ignore these.
            set("LBP3_SHADNET", "1");
            set("LBP3_SHADNET_SERVER", ShadnetServer(s));
        }
    }
    if (s.stats) set("LBP3_STATS", "1");
    if (!s.language.empty()) {
        set("LBP3_LANGUAGE", s.language);
        set("LBP3_LANGUAGES", "all");
    }
    return p;
}

bool ValidOnlineName(const std::string& name) {
    if (name.size() < 3 || name.size() > 16) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

std::string NormalizeServer(const std::string& typed) {
    std::string t = Trim(typed);
    while (!t.empty() && t.back() == '/') t.pop_back();
    if (t.empty()) return {};
    std::string scheme, rest = t;
    if (const size_t p = t.find("://"); p != std::string::npos) {
        scheme = Lower(t.substr(0, p));
        rest = t.substr(p + 3);
        if (scheme != "http" && scheme != "https") return {};
    }
    // host[:port], no path, no spaces
    if (rest.empty() || rest.find_first_of(" /\\?#@") != std::string::npos) return {};
    std::string host = rest;
    if (const size_t c = rest.rfind(':'); c != std::string::npos) {
        host = rest.substr(0, c);
        int port = 0;
        if (!ParseInt(rest.substr(c + 1), 1, 65535, port)) return {};
    }
    if (host.empty()) return {};
    for (char ch : host)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-')) return {};
    if (scheme.empty()) {
        const bool local = Lower(host) == "localhost" || host == "127.0.0.1" || rest.find(':') != std::string::npos;
        scheme = local ? "http" : "https";
    }
    return scheme + "://" + rest;
}

const std::vector<Resolution>& Resolutions() {
    static const std::vector<Resolution> list = {
        {"1280x720", "1280 x 720 (lighter on the GPU)"},
        {"1920x1080", "1920 x 1080 (the PS4's own)"},
        {"2560x1440", "2560 x 1440"},
        {"3840x2160", "3840 x 2160 (4K)"},
        {"2560x1080", "2560 x 1080 (21:9)"},
        {"3440x1440", "3440 x 1440 (21:9)"},
        {"3840x1600", "3840 x 1600 (21:9)"},
        {"3840x1080", "3840 x 1080 (32:9)"},
        {"4480x1260", "4480 x 1260 (32:9)"},
    };
    return list;
}

std::string CanonicalRes(const std::string& v) {
    const std::string l = Lower(v);
    if (l == "1440") return "2560x1440";
    if (l == "1080") return "1920x1080";
    for (const Resolution& r : Resolutions())
        if (l == r.value) return r.value;
    return {};
}

const std::vector<Language>& Languages() {
    static const std::vector<Language> list = {
        {"", "English (US) - the default"},
        {"en-gb", "English (UK)"}, {"fr", "French"}, {"fr-ca", "French (Canada)"}, {"de", "German"},
        {"es", "Spanish"}, {"es-la", "Spanish (Latin America)"}, {"it", "Italian"}, {"nl", "Dutch"},
        {"pt", "Portuguese"}, {"pt-br", "Portuguese (Brazil)"}, {"ru", "Russian"}, {"pl", "Polish"},
        {"fi", "Finnish"}, {"sv", "Swedish"}, {"da", "Danish"}, {"no", "Norwegian"}, {"tr", "Turkish"},
        {"ja", "Japanese"}, {"ko", "Korean"}, {"zh-tw", "Chinese (Traditional)"},
        {"zh-cn", "Chinese (Simplified)"},
    };
    return list;
}

}  // namespace launcher
