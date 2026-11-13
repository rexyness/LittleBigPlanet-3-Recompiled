// What the game's logs say (launcher_stats.h).
#include "launcher_stats.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>

namespace launcher {
namespace fs = std::filesystem;

std::string PrettyName(const std::string& folder) {
    std::string s = folder;
    // a leading "02_", "q01_", "c01_" ordering prefix goes (not a longer number: "2015_party")
    const size_t us = s.find('_');
    if (us != std::string::npos && us <= 3) {
        bool prefix = us > 0;
        for (size_t i = 0; i < us; ++i)
            if (!(std::isdigit(static_cast<unsigned char>(s[i])) || (i == 0 && std::isalpha(static_cast<unsigned char>(s[i])))))
                prefix = false;
        if (prefix && std::isdigit(static_cast<unsigned char>(s[us - 1]))) s = s.substr(us + 1);
    }
    bool up = true;
    for (char& c : s) {
        if (c == '_') {
            c = ' ';
            up = true;
        } else if (up) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            up = false;
        }
    }
    return s;
}

Level LevelFromLogLine(const std::string& line) {
    static const std::string key = "/official_adventure_levels/";
    if (line.compare(0, 10, "[fs] open ") != 0) return {};
    const size_t at = line.find(key);
    if (at == std::string::npos) return {};
    const size_t w0 = at + key.size();
    const size_t w1 = line.find('/', w0);
    if (w1 == std::string::npos) return {};
    const size_t l1 = line.find('/', w1 + 1);
    Level lv;
    lv.world = PrettyName(line.substr(w0, w1 - w0));
    if (l1 != std::string::npos) lv.level = PrettyName(line.substr(w1 + 1, l1 - w1 - 1));
    return lv;
}

bool FlipStatusFromLogLine(const std::string& line, long* flips, double* avg_ms) {
    if (line.compare(0, 8, "[video] ") != 0) return false;
    const size_t f = line.find(" frames flipped");
    if (f == std::string::npos) return false;
    size_t s = f;
    while (s > 8 && std::isdigit(static_cast<unsigned char>(line[s - 1]))) --s;
    if (s == f) return false;
    *flips = std::strtol(line.c_str() + s, nullptr, 10);
    *avg_ms = 0;
    const size_t a = line.find("(avg ", f);
    if (a != std::string::npos) *avg_ms = std::strtod(line.c_str() + a + 5, nullptr);
    return true;
}

namespace {
bool Digits(const std::string& s, size_t at, size_t n) {
    if (at + n > s.size()) return false;
    for (size_t i = 0; i < n; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[at + i]))) return false;
    return true;
}
// "play_best_20261113_0906.txt" -> the stamp's position (after the base and '_'), or npos.
size_t StampAt(const std::string& name) {
    for (const char* base : {"play_launcher_", "play_shadnet_", "play_best_", "play_log_", "play_"}) {
        const size_t n = std::strlen(base);
        if (name.compare(0, n, base) == 0 && Digits(name, n, 8) && name.size() > n + 13 && name[n + 8] == '_' &&
            Digits(name, n + 9, 4))
            return n;
    }
    return std::string::npos;
}
std::int64_t StampTime(const std::string& name, size_t at) {
    std::tm tm{};
    tm.tm_year = std::stoi(name.substr(at, 4)) - 1900;
    tm.tm_mon = std::stoi(name.substr(at + 4, 2)) - 1;
    tm.tm_mday = std::stoi(name.substr(at + 6, 2));
    tm.tm_hour = std::stoi(name.substr(at + 9, 2));
    tm.tm_min = std::stoi(name.substr(at + 11, 2));
    tm.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&tm));
}
}  // namespace

bool IsSessionLog(const std::string& name, bool archived) {
    if (!archived)
        return name == "play_log.txt" || name == "play_best_log.txt" || name == "play_launcher_log.txt" || name == "play_shadnet_log.txt";
    const size_t at = StampAt(name);
    if (at == std::string::npos) return false;
    // the stamp, then ".txt" (or a ".N.txt" when two sessions ended in the same minute)
    const std::string rest = name.substr(at + 13);
    return rest == ".txt" || (rest.size() > 5 && rest[0] == '.' && rest.compare(rest.size() - 4, 4, ".txt") == 0);
}

PlayStats ReadPlayStats(const fs::path& analysis) {
    PlayStats st;
    struct Log {
        fs::path path;
        bool archived;
    };
    std::vector<Log> logs;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(analysis / "play_logs", ec))
        if (IsSessionLog(e.path().filename().string(), true)) logs.push_back({e.path(), true});
    // the newest session isn't archived until the next start
    for (const char* cur : {"play_best_log.txt", "play_log.txt", "play_launcher_log.txt", "play_shadnet_log.txt"})
        if (fs::exists(analysis / cur, ec)) logs.push_back({analysis / cur, false});
    struct Session {
        std::int64_t end = 0;
        long flips = 0;
        double seconds = 0;
        std::string level;
        std::uintmax_t size = 0;
    };
    std::vector<Session> sessions;
    for (const auto& lg : logs) {
        Session s;
        const auto t = fs::last_write_time(lg.path, ec);
        if (ec) continue;
        s.end = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::clock_cast<std::chrono::system_clock>(t).time_since_epoch())
                    .count();
        // an archived log's name has the time it was last written (the session's end)
        const std::string name = lg.path.filename().string();
        if (lg.archived) s.end = StampTime(name, StampAt(name));
        s.size = fs::file_size(lg.path, ec);
        std::ifstream f(lg.path, std::ios::binary);
        std::string line;
        long prev = 0;
        while (std::getline(f, line)) {
            long n = 0;
            double avg = 0;
            if (FlipStatusFromLogLine(line, &n, &avg)) {
                if (n > prev) s.seconds += (n - prev) * avg / 1000.0;
                prev = n;
                s.flips = std::max(s.flips, n);
            } else if (line.compare(0, 10, "[fs] open ") == 0) {
                const Level lv = LevelFromLogLine(line);
                if (lv.ok()) s.level = lv.Text();
            }
        }
        if (s.flips > 0) sessions.push_back(std::move(s));
    }
    // A current log and its archived copy are the same session (same flips and size).
    std::sort(sessions.begin(), sessions.end(), [](const Session& a, const Session& b) { return a.end < b.end; });
    std::vector<Session> unique;
    for (auto& s : sessions) {
        bool dup = false;
        for (const auto& u : unique) dup |= u.flips == s.flips && u.size == s.size;
        if (!dup) unique.push_back(std::move(s));
    }
    double seconds = 0;
    for (const auto& s : unique) {
        ++st.sessions;
        seconds += s.seconds;
    }
    st.seconds = static_cast<std::int64_t>(seconds);
    if (!unique.empty()) {
        st.last_played = unique.back().end;
        for (auto it = unique.rbegin(); it != unique.rend(); ++it)
            if (!it->level.empty()) {
                st.last_level = it->level;
                break;
            }
    }
    return st;
}

std::vector<std::string> LogTail::Poll() {
    std::vector<std::string> out;
    std::error_code ec;
    if (m_path.empty() || !fs::exists(m_path, ec)) return out;
    const std::uintmax_t size = fs::file_size(m_path, ec);
    if (ec) return out;
    if (size < m_offset) m_offset = 0, m_pending.clear();  // a new log replaced it
    if (size == m_offset) return out;
    std::ifstream f(m_path, std::ios::binary);
    f.seekg(static_cast<std::streamoff>(m_offset));
    std::string chunk(static_cast<size_t>(std::min<std::uintmax_t>(size - m_offset, 8u << 20)), '\0');
    f.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<size_t>(f.gcount()));
    m_offset += chunk.size();
    m_pending += chunk;
    for (size_t nl; (nl = m_pending.find('\n')) != std::string::npos;) {
        std::string line = m_pending.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        m_pending.erase(0, nl + 1);
    }
    return out;
}

}  // namespace launcher
