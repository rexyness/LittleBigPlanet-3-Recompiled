// Updates for a packaged install (tools/make_package.ps1's portable zip): the latest GitHub release
// of the fork, its zip checked against its .sha256, unpacked, and the program files replaced in
// place. user\, analysis\ and logs\ (saves, settings, accounts, logs) are never touched. Files that
// are in use (the launcher's own exe and SDL3.dll) are renamed to *.old first and removed on the
// next start. Dev checkouts (a game folder with .git) use the git "Update and build" instead.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher {

constexpr char kReleaseRepo[] = "rexyness/LittleBigPlanet-3-Recompiled";

struct Version {
    int major = 0, minor = 0, patch = 0;
    bool ok = false;
    std::string Text() const;
};
// "v0.1.0-lbp3port" -> 0.1.0 (a leading v and any suffix after the three numbers are ignored).
Version ParseVersion(const std::string& tag);
int CompareVersions(const Version& a, const Version& b);  // <0, 0, >0
// This install's version: LBP3_PORT_VERSION (compiled in) or SOURCE.md's "Package version: v..."
// next to the launcher; empty when neither (a dev build).
std::string InstalledVersion(const std::filesystem::path& launcher_dir);

struct ReleaseInfo {
    bool ok = false;
    std::string tag, name, notes, published;
    std::string zip_name, zip_url, sha_url;
    std::uint64_t zip_size = 0;
    std::string error;
};
// GitHub's releases/latest JSON: the asset LBP3-Recompiled-<tag>.zip and its .sha256 if attached.
ReleaseInfo ParseLatestRelease(const std::string& body);
ReleaseInfo FetchLatestRelease(const std::string& repo = kReleaseRepo);

using Progress = std::function<void(std::uint64_t done, std::uint64_t total)>;
// GET to a file (redirects followed, as GitHub's asset links need). False with *error otherwise.
bool DownloadFile(const std::string& url, const std::filesystem::path& out, const Progress& progress,
                  const std::atomic<bool>* cancel, std::string* error);
std::string Sha256File(const std::filesystem::path& file);  // lower-case hex; empty if unreadable
// The first 64-hex-digit word of a .sha256 file's text, lower case; empty if none.
std::string ParseSha256Text(const std::string& text);

// Unpacks a zip into dest (made if missing). Entries with absolute paths or ".." are refused.
bool ExtractZip(const std::filesystem::path& zip, const std::filesystem::path& dest, std::string* error);
// Copies the unpacked release over the install: every file except under user\, analysis\ and
// logs\. A release zip holding one top folder is entered first. A file that can't be overwritten
// (in use) is renamed to <name>.old first. changed gets the relative paths written.
bool ApplyUpdate(const std::filesystem::path& unpacked, const std::filesystem::path& install, std::vector<std::string>* changed,
                 std::string* error);
// Removes the *.old files a previous update left (the launcher calls it at start).
void RemoveOldFiles(const std::filesystem::path& install);
// A path the update must never write (user\..., analysis\..., logs\...), relative to the install.
bool ProtectedPath(const std::filesystem::path& relative);

}  // namespace launcher
