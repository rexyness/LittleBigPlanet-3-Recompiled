// "Check for updates" and "Update and build" for this source-built setup: the build's git
// worktree (lbp3.exe\..\..), its branch, and a remote (fork by default). Checking only fetches;
// updating fast-forwards the worktree (and its shadPS4 to the commit the update names), then runs
// build.bat. Never touches the saves; the caller refuses while the game runs.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher {

struct Change {
    std::string sha;      // short
    std::string when;     // yyyy-mm-dd
    std::string subject;
};

struct UpdateInfo {
    bool ok = false;
    std::string error;
    std::string branch;   // the worktree's branch
    std::string head;     // its commit (short)
    std::string remote;
    std::vector<Change> incoming;  // on <remote>/<branch>, not here yet (newest first)
    bool dirty = false;            // local changes: updating refuses
};

using LineFn = std::function<void(const std::string&)>;

// git fetch <remote> <branch>, then the commits it brought.
UpdateInfo CheckUpdates(const std::filesystem::path& worktree, const std::string& remote, const LineFn& log,
                        const std::atomic<bool>* cancel);
// git pull --ff-only, the shadPS4 folder moved to the commit the update names (fast-forward or
// detached checkout), then build.bat. True when every step succeeded.
// game_only builds just lbp3.exe (when the running launcher lives in that build folder, whose
// LBP3Launcher.exe can't be replaced while it runs). game_running is asked before the pull and
// again before the build: the update stops if the game is running (it never touches saves).
// A third_party\shadPS4 that is a junction (shared by several worktrees) is left alone.
bool UpdateAndBuild(const std::filesystem::path& worktree, const std::string& remote, bool game_only,
                    const std::function<bool()>& game_running, const LineFn& log, const std::atomic<bool>* cancel,
                    std::string* error);
// The last `n` commits of the worktree (for "What's new").
std::vector<Change> RecentChanges(const std::filesystem::path& worktree, int n);
// Parses `git log --format=%h%x09%ad%x09%s --date=short` output.
std::vector<Change> ParseLog(const std::string& text);

}  // namespace launcher
