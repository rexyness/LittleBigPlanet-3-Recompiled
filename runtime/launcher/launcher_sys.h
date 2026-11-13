// LBP3Launcher's Windows side: where things are, the checks, saves backups, the log rotation, the
// game process (with the settings as LBP3_* variables), the Quit Game setup step and the report
// zip. No ImGui here; launcher_main.cpp draws the window.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "launcher_settings.h"

namespace launcher {

namespace fs = std::filesystem;

// Where everything is for the current settings.
struct Paths {
    fs::path launcher_dir;  // the folder LBP3Launcher.exe is in
    fs::path root;          // the game folder: user\, analysis\, game_dir.txt (the working folder)
    fs::path exe;           // the lbp3.exe to start
    fs::path worktree;      // exe\..\.. (its tools\quit_menu_setup.py, user\file_edits.bin)
    fs::path game_dir;      // the folder holding CUSA00473 (the dump)
    fs::path dlc_dir;       // <game_dir>\CUSA00473-DLC
    fs::path cache_dir;     // LBP3_SHADER_CACHE_DIR, empty = the normal user\shader_cache
    fs::path saves;         // <root>\user\savedata\CUSA00473
    fs::path backups;       // <root>\analysis\savedata_backups
    std::string log_base;   // "play_best", "play" or "play_launcher" (analysis\<base>_log.txt)
};

fs::path LauncherDir();
// The game folder: the setting if given; else the launcher's parent folder if it has
// game_dir.txt or user\savedata; else, when the launcher is built in a git worktree, the main
// checkout that worktree belongs to (its .git file names it).
fs::path FindRoot(const fs::path& launcher_dir, const std::string& setting);
// The main checkout a worktree's .git file ("gitdir: X/.git/worktrees/name") belongs to; empty
// if the text isn't such a file.
fs::path MainCheckoutFromGitFile(const std::string& git_file_text);
Paths ResolvePaths(const Settings& s);

struct Check {
    bool ok;
    std::string what;    // "Game files", "DLC folder"...
    std::string detail;  // the path, or what's wrong
};
std::vector<Check> RunChecks(const Settings& s, const Paths& p);

// Is any lbp3.exe running (this launcher's or not)?
bool GameRunning();
// A scripted test has the player's saves set aside (analysis\savedata_live_is_test).
bool TestMarkerSet(const Paths& p);

// Saves backups: analysis\savedata_backups\<name>, each a copy of user\savedata\CUSA00473.
struct Backup {
    std::string name;
    fs::path path;
    std::int64_t time = 0;  // last write, seconds since 1970
};
std::vector<Backup> ListBackups(const Paths& p);  // newest first
// Copies the saves to analysis\savedata_backups\<prefix>_<yyyyMMdd_HHmmss>; returns its name, or
// "" with *error set.
std::string BackupSaves(const Paths& p, const std::string& prefix, std::string* error);
// Backs the current saves up first, then replaces them with the backup. False with *error set if
// anything went wrong (the current saves are then left as they were, or put back).
bool RestoreBackup(const Paths& p, const Backup& b, std::string* error, std::string* safety_backup);

// Rotates analysis\<base>_log.txt to .1 and .2 and keeps a copy of it, named by its last write,
// in analysis\play_logs\ (as play_best.bat does).
void RotateLogs(const Paths& p);
fs::path LatestLog(const Paths& p);

// The Quit Game setup (play_best.bat's step): python <worktree>\tools\quit_menu_setup.py, which
// writes <worktree>\user\file_edits.bin. Returns the variables to add (empty when it can't run or
// failed; *note says why).
EnvList QuitMenuSetup(const Paths& p, std::string* note);
// Is Python (python.exe on PATH) there?
bool PythonFound();
// python.exe's full path (not the Microsoft Store stub); empty if there is none.
std::filesystem::path PythonPath();

// user\config\online_account.txt of the game folder.
std::filesystem::path AccountFilePath(const Paths& p);
// The settings with an empty online name or server filled in from the account file (what the
// game is started with).
Settings EffectiveOnline(const Settings& s, const Paths& p);

// Everything LBP3_* the game gets: the mapped settings, plus LBP3_GAME_DIR (from game_dir.txt, as
// play_best.bat), LBP3_SHADER_CACHE_DIR and the given extra pairs (Quit Game's).
EnvList GameEnvironment(const Settings& s, const Paths& p, const EnvList& extra);

// Starts the game: working folder = root, stdout/stderr into analysis\<base>_stdout.txt and
// <base>_log.txt, every LBP3_* from the launcher's own environment replaced by `env`. Returns the
// process handle (0 on failure, *error set).
std::uintptr_t StartGame(const Paths& p, const EnvList& env, const std::vector<std::string>& args,
                         std::string* error);
// Exit code once the process has ended; -1 while it runs.
long GameExitCode(std::uintptr_t process);
void CloseGameHandle(std::uintptr_t process);

// Opens a folder or file in Explorer / its default program.
void OpenPath(const fs::path& path);

// A "stored" (uncompressed) zip of the given files, each under its own name. CRC32 as zip needs.
std::uint32_t Crc32(const void* data, size_t size);
bool WriteZip(const fs::path& zip, const std::vector<std::pair<std::string, std::string>>& name_and_data,
              std::string* error);
// analysis\reports\report_<time>.zip: the latest logs, launcher.ini, system information; no saves
// and no online password. Returns the zip's path, or empty with *error set.
fs::path MakeReport(const Settings& s, const Paths& p, std::string* error);

// launcher.ini (user\config\launcher.ini in the game folder; next to the launcher when there is no
// game folder).
fs::path SettingsFile(const fs::path& root);
Settings LoadSettings();
bool SaveSettings(const Settings& s);

std::string NowStamp();  // yyyyMMdd_HHmmss, local time
std::string ReadFile(const fs::path& path);
// One line in analysis\launcher_log.txt (what the launcher did: starts, backups, restores).
void AppendLauncherLog(const Paths& p, const std::string& line);

}  // namespace launcher
