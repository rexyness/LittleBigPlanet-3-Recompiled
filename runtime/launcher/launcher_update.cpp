// The updater (launcher_update.h).
#include "launcher_update.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <sstream>

#include "launcher_proc.h"

namespace launcher {
namespace fs = std::filesystem;
namespace {

std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // ASCII arguments only

std::string Trim(std::string s) {
    while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ') s.pop_back();
    return s;
}

// git -C <dir> <args...>
ProcResult Git(const fs::path& dir, const std::vector<std::string>& args, const LineFn& log, const std::atomic<bool>* cancel,
               unsigned timeout_ms = 120000) {
    fs::path git = FindOnPath(L"git.exe");
    if (git.empty()) {
        ProcResult r;
        r.error = "git isn't installed (or not on PATH)";
        return r;
    }
    std::wstring cmd = Quote(git.wstring()) + L" -C " + Quote(dir.wstring());
    std::string shown = "git";
    for (const auto& a : args) {
        cmd += L" " + Quote(W(a));
        shown += " " + a;
    }
    if (log) log("> " + shown);
    return RunCapture(cmd, dir, log, cancel, timeout_ms);
}

bool Ok(const ProcResult& r) { return r.started && r.exit_code == 0; }

}  // namespace

std::vector<Change> ParseLog(const std::string& text) {
    std::vector<Change> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t a = line.find('\t');
        if (a == std::string::npos) continue;
        const size_t b = line.find('\t', a + 1);
        if (b == std::string::npos) continue;
        out.push_back({line.substr(0, a), line.substr(a + 1, b - a - 1), line.substr(b + 1)});
    }
    return out;
}

UpdateInfo CheckUpdates(const fs::path& wt, const std::string& remote, const LineFn& log, const std::atomic<bool>* cancel) {
    UpdateInfo u;
    u.remote = remote;
    ProcResult r = Git(wt, {"rev-parse", "--abbrev-ref", "HEAD"}, nullptr, cancel);
    if (!Ok(r)) {
        u.error = r.error.empty() ? "this build's folder isn't a git checkout (" + wt.string() + ")" : r.error;
        return u;
    }
    u.branch = Trim(r.output);
    if (u.branch == "HEAD") {
        u.error = "this build's folder isn't on a branch";
        return u;
    }
    u.head = Trim(Git(wt, {"rev-parse", "--short", "HEAD"}, nullptr, cancel).output);
    u.dirty = !Trim(Git(wt, {"status", "--porcelain", "--ignore-submodules=all", "-uno"}, nullptr, cancel).output).empty();
    r = Git(wt, {"fetch", remote, u.branch}, log, cancel);
    if (!Ok(r)) {
        u.error = "couldn't fetch " + remote + "/" + u.branch + (r.error.empty() ? "" : ": " + r.error);
        return u;
    }
    r = Git(wt, {"log", "--format=%h%x09%ad%x09%s", "--date=short", "HEAD.." + remote + "/" + u.branch}, nullptr, cancel);
    if (!Ok(r)) {
        u.error = "couldn't compare with " + remote + "/" + u.branch;
        return u;
    }
    u.incoming = ParseLog(r.output);
    u.ok = true;
    return u;
}

bool UpdateAndBuild(const fs::path& wt, const std::string& remote, bool game_only, const std::function<bool()>& game_running,
                    const LineFn& log, const std::atomic<bool>* cancel, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        if (log) log("! " + e);
        return false;
    };
    if (game_running && game_running()) return fail("the game is running; close it first");
    UpdateInfo u = CheckUpdates(wt, remote, log, cancel);
    if (!u.ok) return fail(u.error);
    if (u.dirty) return fail("this build's folder has local changes; update it by hand");
    if (game_running && game_running()) return fail("the game is running; close it first");
    if (!u.incoming.empty()) {
        if (!Ok(Git(wt, {"pull", "--ff-only", remote, u.branch}, log, cancel, 600000)))
            return fail("git pull --ff-only failed (the branch has moved in a way that needs a hand merge)");
    } else if (log) {
        log("Already up to date; building anyway.");
    }
    // shadPS4: the commit the runtime names.
    const fs::path sub = wt / "third_party" / "shadPS4";
    std::error_code ec;
    const DWORD attrs = GetFileAttributesW(sub.c_str());
    const bool shared = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT);
    if (shared && log) log("third_party/shadPS4 is shared with other worktrees (a junction): left as it is");
    if (!shared && fs::exists(sub / ".git", ec)) {
        const std::string tree = Trim(Git(wt, {"ls-tree", "HEAD", "third_party/shadPS4"}, nullptr, cancel).output);
        // "160000 commit <sha>\tthird_party/shadPS4"
        const size_t c = tree.find("commit ");
        const std::string want = c == std::string::npos ? "" : tree.substr(c + 7, 40);
        const std::string have = Trim(Git(sub, {"rev-parse", "HEAD"}, nullptr, cancel).output);
        if (!want.empty() && want != have) {
            if (log) log("shadPS4 moves to " + want.substr(0, 10));
            Git(sub, {"fetch", remote}, log, cancel, 600000);
            const std::string branch = Trim(Git(sub, {"rev-parse", "--abbrev-ref", "HEAD"}, nullptr, cancel).output);
            bool moved = false;
            if (branch != "HEAD") moved = Ok(Git(sub, {"merge", "--ff-only", want}, log, cancel));
            if (!moved) moved = Ok(Git(sub, {"checkout", "--detach", want}, log, cancel));
            if (!moved) return fail("couldn't move shadPS4 to " + want.substr(0, 10) + "; update it by hand");
        }
    }
    if (cancel && cancel->load()) return fail("cancelled");
    if (game_running && game_running()) return fail("the game is running; close it first (the update is in, the build isn't)");
    const fs::path bat = wt / "build.bat";
    if (!fs::exists(bat, ec)) return fail("no build.bat in " + wt.string());
    if (log) log(game_only ? "> build.bat --target lbp3" : "> build.bat");
    const ProcResult b = RunCapture(L"cmd.exe /d /c " + Quote(bat.wstring()) + (game_only ? L" --target lbp3" : L""), wt, log, cancel, 0);
    if (!Ok(b)) return fail("the build failed" + (b.error.empty() ? std::string() : ": " + b.error) + "; see the log above");
    if (log) log("Build finished.");
    return true;
}

std::vector<Change> RecentChanges(const fs::path& wt, int n) {
    const ProcResult r = Git(wt, {"log", "-" + std::to_string(n), "--format=%h%x09%ad%x09%s", "--date=short"}, nullptr, nullptr, 20000);
    return Ok(r) ? ParseLog(r.output) : std::vector<Change>{};
}

}  // namespace launcher
