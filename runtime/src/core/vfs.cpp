#include "core/vfs.h"

#include <atomic>
#include <cstdio>
#include <map>
#include <mutex>
#include <system_error>

namespace core::vfs {
namespace {

namespace fs = std::filesystem;

std::vector<fs::path> g_app0;
fs::path g_user;

struct DynamicMount { fs::path host; bool writable; };
std::mutex g_mountMutex;
std::map<std::string, DynamicMount> g_mounts;

// Writable mount points and the user/ subfolder they live in.
const char* const kUserMounts[] = {"/temp0", "/temp", "/data", "/download0"};

// Collapses "//", "." and ".."; returns false if ".." climbs above the root.
bool Normalize(std::string_view in, std::string& out) {
    std::string path = in.empty() || in[0] != '/' ? "/app0/" + std::string(in) : std::string(in);
    std::vector<std::string_view> parts;
    size_t i = 0;
    while (i < path.size()) {
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        std::string_view part(path.data() + i, j - i);
        if (part == "..") {
            if (parts.size() <= 1) return false;  // keep the mount point
            parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        i = j + 1;
    }
    out.clear();
    for (auto p : parts) {
        out += '/';
        out += p;
    }
    if (out.empty()) out = "/";
    return true;
}

bool HasMount(const std::string& path, std::string_view mount, std::string& rest) {
    if (path.compare(0, mount.size(), mount) != 0) return false;
    if (path.size() > mount.size() && path[mount.size()] != '/') return false;
    rest = path.size() > mount.size() ? path.substr(mount.size() + 1) : "";
    return true;
}

// Skip-intro: the startup logo and intro movies are served as the game's own
// 276-byte placeholder video, which "plays" instantly.
bool g_skipIntro = false;
bool IsStartupVideo(const std::string& rest) {
    static const char* const kVideos[] = {"scea.bik", "scee.bik", "scej.bik", "scek.bik", "sumo_digital.bik",
                                          "lbp3_intro_live_action.bik"};
    const std::string dir = "gamedata_orbis/videos/lbp3/";
    if (rest.compare(0, dir.size(), dir) != 0) return false;
    for (const char* v : kVideos)
        if (rest.compare(dir.size(), std::string::npos, v) == 0) return true;
    return false;
}

// The first app0 layer that has `rest`, or an empty path.
fs::path FindInApp0(const std::string& rest) {
    std::error_code ec;
    for (const auto& layer : g_app0) {
        fs::path p = rest.empty() ? layer : layer / fs::u8path(rest);
        if (fs::exists(p, ec)) return p;
    }
    return {};
}

}  // namespace

// The idea and the three path rules are LBPPC's (emurphy04, GPL-3.0-or-later, e9b6b4e,
// src/runtime_file.c english_voice, their issue #35); the code here is our own, and the folder
// names and bank names were checked against a US 1.28 copy's gamedata/audio.
std::vector<std::string> EnglishVoiceCandidates(std::string_view rest) {
    static constexpr std::string_view kLanguages[] = {
        "arabic", "braz_portuguese", "danish", "dutch", "finnish", "french", "german", "italian",
        "latam_spanish", "norwegian", "polish", "portuguese", "russian", "spanish", "swedish"};
    constexpr std::string_view kAudio = "gamedata/audio/";
    constexpr std::string_view kDialogue = "dialogue/";
    if (rest.substr(0, kAudio.size()) != kAudio) return {};
    std::string_view tail = rest.substr(kAudio.size());
    const bool dialogue = tail.substr(0, kDialogue.size()) == kDialogue;
    if (dialogue) tail.remove_prefix(kDialogue.size());
    const std::string before(rest.substr(0, rest.size() - tail.size()));  // up to the language folder
    for (std::string_view language : kLanguages) {
        if (tail.size() <= language.size() + 1 || tail.substr(0, language.size()) != language ||
            tail[language.size()] != '/')
            continue;
        const std::string_view file = tail.substr(language.size() + 1);
        if (dialogue) return {before + std::string(file)};
        if (file.find('/') != std::string_view::npos) return {};
        std::vector<std::string> out;
        // <bank>_<xxx>.fsb: the three letters name the language (fre, ger, spa...).
        if (file.size() > 8 && file[file.size() - 8] == '_' && file.substr(file.size() - 4) == ".fsb")
            out.push_back(before + std::string(file.substr(0, file.size() - 8)) + "_eng.fsb");
        out.push_back(before + std::string(file));
        return out;
    }
    return {};
}

void SetSkipIntro(bool skip) { g_skipIntro = skip; }

void Init(std::vector<fs::path> app0Layers, fs::path userRoot) {
    g_app0 = std::move(app0Layers);
    g_user = std::move(userRoot);
    std::error_code ec;
    for (const char* m : kUserMounts) fs::create_directories(g_user / (m + 1), ec);
}

fs::path UserRoot() { return g_user; }

void Mount(const std::string& mountPoint, const fs::path& host, bool writable) {
    std::lock_guard lock(g_mountMutex);
    g_mounts[mountPoint] = {host, writable};
}

void Unmount(const std::string& mountPoint) {
    std::lock_guard lock(g_mountMutex);
    g_mounts.erase(mountPoint);
}

bool IsMounted(const std::string& mountPoint) {
    std::lock_guard lock(g_mountMutex);
    return g_mounts.contains(mountPoint);
}

Resolved Resolve(std::string_view guestPath) {
    Resolved r;
    if (!Normalize(guestPath, r.guest)) return r;
    std::string rest;
    std::error_code ec;

    {
        std::lock_guard lock(g_mountMutex);
        for (const auto& [mount, target] : g_mounts) {
            if (!HasMount(r.guest, mount, rest)) continue;
            r.host = rest.empty() ? target.host : target.host / fs::u8path(rest);
            r.writable = target.writable;
            r.status = fs::exists(r.host, ec) ? Status::Ok : Status::NotFound;
            return r;
        }
    }

    if (HasMount(r.guest, "/app0", rest) || HasMount(r.guest, "/hostapp", rest)) {
        if (g_skipIntro && IsStartupVideo(rest)) rest = "gamedata/videos/empty_video.bik";
        if (fs::path p = FindInApp0(rest); !p.empty()) {
            r.host = p;
            r.status = Status::Ok;
            return r;
        }
        // A voice bank or dialogue file this copy doesn't have for the language (the US copy
        // carries story voices in English and French only, so with LBP3_LANGUAGES=all the
        // characters were silent in the others): the English recording instead of nothing.
        for (const std::string& english : EnglishVoiceCandidates(rest)) {
            fs::path p = FindInApp0(english);
            if (p.empty()) continue;
            static std::atomic<bool> told{false};
            if (!told.exchange(true)) {
                std::fprintf(stderr, "[vfs] no voices for this language in this copy, English voices instead (%s -> %s)\n",
                             rest.c_str(), english.c_str());
            }
            r.host = p;
            r.status = Status::Ok;
            return r;
        }
        r.host = g_app0.empty() ? fs::path() : g_app0.back() / fs::u8path(rest);
        r.status = Status::NotFound;
        return r;
    }
    for (const char* m : kUserMounts) {
        if (!HasMount(r.guest, m, rest)) continue;
        fs::path base = g_user / (std::string_view(m) == "/temp" ? "temp0" : m + 1);
        r.host = rest.empty() ? base : base / fs::u8path(rest);
        r.writable = true;
        r.status = fs::exists(r.host, ec) ? Status::Ok : Status::NotFound;
        return r;
    }
    return r;  // unknown mount
}

std::vector<fs::path> DirectoryLayers(const Resolved& r) {
    std::string rest;
    if (!HasMount(r.guest, "/app0", rest) && !HasMount(r.guest, "/hostapp", rest)) return {r.host};
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& layer : g_app0) {
        fs::path p = rest.empty() ? layer : layer / fs::u8path(rest);
        if (fs::is_directory(p, ec)) out.push_back(p);
    }
    return out;
}

}  // namespace core::vfs
