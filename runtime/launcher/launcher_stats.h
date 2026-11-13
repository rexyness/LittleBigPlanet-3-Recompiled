// What the game's logs say: the level being played (from the [fs] opens of level archives), play
// time per session (the [video] flip counter at 30 a second), and a reader that follows a log
// while the game writes it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace launcher {

struct Level {
    std::string world;  // "Manglewood"
    std::string level;  // "Diner"
    bool ok() const { return !world.empty(); }
    std::string Text() const { return level.empty() ? world : world + " - " + level; }
};
// "[fs] open /app0/gamedata/lbp3/official_adventure_levels/02_manglewood/04_diner/diner.farc ..."
Level LevelFromLogLine(const std::string& line);
// "02_manglewood" -> "Manglewood", "01_needlepoint_peaks" -> "Needlepoint Peaks"
std::string PrettyName(const std::string& folder);
// "[video] 25050 frames flipped (avg 36.4 ms, ..." -> 25050 and 36.4. The average covers the
// flips since the previous status line, so (flips - previous flips) * avg is that stretch's real
// time, with or without frame generation (it counts the game's own flips). False otherwise.
bool FlipStatusFromLogLine(const std::string& line, long* flips, double* avg_ms);

// The play sessions' logs: the current ones (play_log.txt, play_best_log.txt,
// play_launcher_log.txt, play_shadnet_log.txt) and their archived copies in play_logs (play_log_,
// play_best_, play_launcher_, play_shadnet_ or play_ and a yyyyMMdd_HHmm stamp). Not the stdout
// logs or test runs.
bool IsSessionLog(const std::string& file_name, bool archived);

struct PlayStats {
    int sessions = 0;
    std::int64_t seconds = 0;        // all sessions
    std::int64_t last_played = 0;    // unix time of the newest session's end
    std::string last_level;          // the newest session's last level
};
// From analysis\play_logs\ (every archived session) and the current logs.
PlayStats ReadPlayStats(const std::filesystem::path& analysis_dir);

// Follows a log as it grows: Poll() returns the complete new lines since the last call.
class LogTail {
public:
    explicit LogTail(std::filesystem::path path = {}) : m_path(std::move(path)) {}
    void Reset(std::filesystem::path path) {
        m_path = std::move(path);
        m_offset = 0;
        m_pending.clear();
    }
    std::vector<std::string> Poll();

private:
    std::filesystem::path m_path;
    std::uint64_t m_offset = 0;
    std::string m_pending;
};

}  // namespace launcher
