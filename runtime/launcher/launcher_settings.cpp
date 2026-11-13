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
      << "[game]\n"
      << "skip_intro=" << b(s.skip_intro) << "\n"
      << "framegen=" << b(s.framegen) << "\n"
      << "res=" << s.res << "\n"
      << "my_unlocks=" << b(s.my_unlocks) << "\n"
      << "bloom_1440=" << b(s.bloom_1440) << "\n"
      << "fg_text=" << b(s.fg_text) << "\n"
      << "status_flips=" << s.status_flips << "\n"
      << "; 0 = all CPU cores but one\n"
      << "pipeline_threads=" << s.pipeline_threads << "\n"
      << "vk_pipeline_cache=" << b(s.vk_pipeline_cache) << "\n"
      << "menu=" << b(s.menu) << "\n"
      << "stats=" << b(s.stats) << "\n"
      << "quit_menu=" << b(s.quit_menu) << "\n"
      << "; empty = English; a code such as de, fr, ja, pt-br\n"
      << "language=" << OneLine(s.language) << "\n"
      << "motion_filter=" << b(s.motion_filter) << "\n"
      << "tilt_keys=" << b(s.tilt_keys) << "\n"
      << "mouse_touch=" << b(s.mouse_touch) << "\n"
      << "[online]\n"
      << "; off unless 1, and only with a server and a name\n"
      << "online=" << b(s.online) << "\n"
      << "server=" << OneLine(s.online_server) << "\n"
      << "name=" << OneLine(s.online_name) << "\n";
    return o.str();
}

Settings FromIni(const std::string& text) {
    Settings s;
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
        } else if (section == "game") {
            if (key == "skip_intro") ParseBool(v, s.skip_intro);
            else if (key == "framegen") ParseBool(v, s.framegen);
            else if (key == "res") {
                if (ParseInt(v, 0, 10000, n) && (n == 1440 || n == 1080)) s.res = n;
            } else if (key == "my_unlocks") ParseBool(v, s.my_unlocks);
            else if (key == "bloom_1440") ParseBool(v, s.bloom_1440);
            else if (key == "fg_text") ParseBool(v, s.fg_text);
            else if (key == "status_flips") {
                if (ParseInt(v, 1, 100000, n)) s.status_flips = n;
            } else if (key == "pipeline_threads") {
                if (v.empty()) s.pipeline_threads = 0;
                else if (ParseInt(v, 0, 256, n)) s.pipeline_threads = n;
            } else if (key == "vk_pipeline_cache") ParseBool(v, s.vk_pipeline_cache);
            else if (key == "menu") ParseBool(v, s.menu);
            else if (key == "stats") ParseBool(v, s.stats);
            else if (key == "quit_menu") ParseBool(v, s.quit_menu);
            else if (key == "language") s.language = Lower(v);
            else if (key == "motion_filter") ParseBool(v, s.motion_filter);
            else if (key == "tilt_keys") ParseBool(v, s.tilt_keys);
            else if (key == "mouse_touch") ParseBool(v, s.mouse_touch);
        } else if (section == "online") {
            if (key == "online") ParseBool(v, s.online);
            else if (key == "server") s.online_server = v;
            else if (key == "name") s.online_name = v;
        }
    }
    return s;
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
    if (s.res == 1440) {
        set("LBP3_RES", "1440");
        set("LBP3_RES_SHADERS", "1");
        if (!s.bloom_1440) set("LBP3_RES_SKIP", "bloom");
    }
    if (s.my_unlocks) set("LBP3_MY_UNLOCKS", "lbp1,lbp2");
    if (!s.fg_text) set("LBP3_FRAMEGEN_TEXT", "off");
    set("LBP3_STATUS_FLIPS", std::to_string(s.status_flips));
    if (s.pipeline_threads > 0) set("LBP3_PIPELINE_THREADS", std::to_string(s.pipeline_threads));
    if (!s.vk_pipeline_cache) set("LBP3_VK_PIPELINE_CACHE", "off");
    if (!s.menu) set("LBP3_MENU", "off");
    if (!s.motion_filter) set("LBP3_MOTION", "raw");
    if (!s.tilt_keys) set("LBP3_TILT_KEYS", "off");
    if (s.mouse_touch) set("LBP3_MOUSE_TOUCH", "1");
    if (s.online) {
        const std::string server = NormalizeServer(s.online_server);
        // Online needs both: without them the game would sign in to nothing.
        if (!server.empty() && ValidOnlineName(s.online_name)) {
            set("LBP3_ONLINE", "1");
            set("LBP3_ONLINE_SERVER", server);
            set("LBP3_ONLINE_NAME", s.online_name);
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
