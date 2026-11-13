// Updates for a packaged install (launcher_release.h).
#include "launcher_release.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "launcher_account.h"  // HttpJson
#include "miniz.h"

#ifndef LBP3_PORT_VERSION
#define LBP3_PORT_VERSION ""
#endif

namespace launcher {
namespace fs = std::filesystem;
using json = nlohmann::json;

std::string Version::Text() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

Version ParseVersion(const std::string& tag) {
    Version v;
    size_t i = 0;
    while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
    if (i < tag.size() && (tag[i] == 'v' || tag[i] == 'V')) ++i;
    int* parts[] = {&v.major, &v.minor, &v.patch};
    for (int k = 0; k < 3; ++k) {
        if (k) {
            if (i >= tag.size() || tag[i] != '.') return Version{};
            ++i;
        }
        if (i >= tag.size() || !std::isdigit(static_cast<unsigned char>(tag[i]))) return Version{};
        int n = 0;
        while (i < tag.size() && std::isdigit(static_cast<unsigned char>(tag[i])) && n < 100000) n = n * 10 + (tag[i++] - '0');
        *parts[k] = n;
    }
    v.ok = true;
    return v;
}

int CompareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

std::string InstalledVersion(const fs::path& dir) {
    if (*LBP3_PORT_VERSION) return LBP3_PORT_VERSION;
    std::ifstream f(dir / "SOURCE.md");
    std::string line;
    const std::string key = "Package version:";
    while (std::getline(f, line)) {
        const size_t at = line.find(key);
        if (at == std::string::npos) continue;
        std::string v = line.substr(at + key.size());
        while (!v.empty() && (v.front() == ' ' || v.front() == '`')) v.erase(v.begin());
        size_t end = 0;
        while (end < v.size() && !std::isspace(static_cast<unsigned char>(v[end])) && v[end] != '`') ++end;
        v.resize(end);
        while (!v.empty() && (v.back() == '.' || v.back() == ',')) v.pop_back();
        return v;
    }
    return {};
}

ReleaseInfo ParseLatestRelease(const std::string& body) {
    ReleaseInfo r;
    const json j = json::parse(body, nullptr, false);
    if (!j.is_object()) {
        r.error = "GitHub's answer wasn't readable";
        return r;
    }
    auto str = [&](const json& o, const char* k) {
        auto it = o.find(k);
        return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    r.tag = str(j, "tag_name");
    r.name = str(j, "name");
    r.notes = str(j, "body");
    r.published = str(j, "published_at");
    if (r.published.size() > 10) r.published.resize(10);
    if (r.tag.empty()) {
        r.error = str(j, "message").empty() ? "no release found" : "GitHub: " + str(j, "message");
        return r;
    }
    // the tag goes into file names and a command line: letters, digits, . _ - only
    for (char c : r.tag)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            r.error = "the release's tag isn't a plain version name";
            return r;
        }
    const std::string want = "LBP3-Recompiled-" + r.tag + ".zip";
    if (auto assets = j.find("assets"); assets != j.end() && assets->is_array()) {
        for (const json& a : *assets) {
            if (!a.is_object()) continue;
            const std::string n = str(a, "name"), url = str(a, "browser_download_url");
            if (n == want) {
                r.zip_name = n;
                r.zip_url = url;
                if (auto s = a.find("size"); s != a.end() && s->is_number_unsigned()) r.zip_size = s->get<std::uint64_t>();
            } else if (n == want + ".sha256") {
                r.sha_url = url;
            }
        }
    }
    if (r.zip_url.empty()) {
        r.error = "the release " + r.tag + " has no " + want;
        return r;
    }
    r.ok = true;
    return r;
}

ReleaseInfo FetchLatestRelease(const std::string& repo) {
    const HttpReply h = HttpJson("GET", "https://api.github.com/repos/" + repo + "/releases/latest", "", "");
    if (h.status == 404) {
        ReleaseInfo r;
        r.error = "no release has been published yet";
        return r;
    }
    if (h.status != 200) {
        ReleaseInfo r;
        r.error = h.status == 0 ? "GitHub didn't answer (" + h.error + ")" : "GitHub said no (HTTP " + std::to_string(h.status) + ")";
        return r;
    }
    return ParseLatestRelease(h.body);
}

namespace {
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};
std::wstring W(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}
}  // namespace

bool DownloadFile(const std::string& url, const fs::path& out, const Progress& progress, const std::atomic<bool>* cancel,
                  std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        std::error_code ec;
        fs::remove(out, ec);
        return false;
    };
    std::wstring wurl = W(url);
    URL_COMPONENTS uc{sizeof uc};
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return fail("not a URL: " + url);
    Handle session{WinHttpOpen(L"LBP3Launcher/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return fail("couldn't start the download");
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 60000);
    Handle connect{WinHttpConnect(session.h, host, uc.nPort, 0)};
    if (!connect.h) return fail("couldn't connect");
    const std::wstring full = std::wstring(path) + extra;
    Handle req{WinHttpOpenRequest(connect.h, L"GET", full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!req.h) return fail("couldn't start the download");
    if (!WinHttpSendRequest(req.h, L"Accept: application/octet-stream\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr))
        return fail("the download didn't start (offline?)");
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return fail("the download was refused (HTTP " + std::to_string(status) + ")");
    std::uint64_t total = 0;
    wchar_t len[32] = {};
    DWORD lenSize = sizeof len;
    if (WinHttpQueryHeaders(req.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &lenSize, WINHTTP_NO_HEADER_INDEX))
        total = _wcstoui64(len, nullptr, 10);
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return fail("can't write " + out.string());
    std::uint64_t done = 0;
    std::vector<char> buf(1 << 16);
    for (;;) {
        if (cancel && cancel->load()) {
            f.close();
            return fail("cancelled");
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail)) {
            f.close();
            return fail("the download broke off");
        }
        if (avail == 0) break;
        DWORD read = 0;
        if (!WinHttpReadData(req.h, buf.data(), std::min<DWORD>(avail, static_cast<DWORD>(buf.size())), &read) || read == 0) {
            f.close();
            return fail("the download broke off");
        }
        f.write(buf.data(), read);
        done += read;
        if (progress) progress(done, total);
    }
    f.close();
    if (!f || (total && done != total)) return fail("the download is incomplete");
    return true;
}

std::string Sha256File(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return {};
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &h, nullptr, 0, nullptr, 0, 0) != 0) return {};
    std::vector<char> buf(1 << 16);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize n = f.gcount();
        if (n > 0) BCryptHashData(h, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(n), 0);
    }
    unsigned char out[32];
    BCryptFinishHash(h, out, sizeof out, 0);
    BCryptDestroyHash(h);
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (unsigned char b : out) s += std::string{hex[b >> 4], hex[b & 15]};
    return s;
}

std::string ParseSha256Text(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !std::isxdigit(static_cast<unsigned char>(text[i]))) ++i;
        size_t j = i;
        while (j < text.size() && std::isxdigit(static_cast<unsigned char>(text[j]))) ++j;
        if (j - i == 64 && (j == text.size() || !std::isalnum(static_cast<unsigned char>(text[j])))) {
            std::string h = text.substr(i, 64);
            for (char& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return h;
        }
        i = j + (j == i);
    }
    return {};
}

bool ExtractZip(const fs::path& zip, const fs::path& dest, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    mz_zip_archive a{};
    if (!mz_zip_reader_init_file(&a, zip.string().c_str(), 0)) return fail("the download isn't a readable zip");
    std::error_code ec;
    fs::create_directories(dest, ec);
    const mz_uint n = mz_zip_reader_get_num_files(&a);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&a, i, &st)) {
            ok = fail("the zip's index is damaged");
            break;
        }
        std::string name = st.m_filename;
        for (char& c : name)
            if (c == '\\') c = '/';
        const fs::path rel = fs::u8path(name);
        bool bad = name.empty() || name[0] == '/' || rel.has_root_name() || rel.has_root_directory();
        for (const auto& part : rel) bad |= part == "..";
        if (bad) {
            ok = fail("the zip has an unsafe path: " + name);
            break;
        }
        const fs::path out = dest / rel;
        if (mz_zip_reader_is_file_a_directory(&a, i)) {
            fs::create_directories(out, ec);
            continue;
        }
        fs::create_directories(out.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&a, i, out.string().c_str(), 0)) ok = fail("couldn't unpack " + name);
    }
    mz_zip_reader_end(&a);
    return ok;
}

bool ProtectedPath(const fs::path& rel) {
    if (rel.empty()) return true;
    std::string top = rel.begin()->string();
    for (char& c : top) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return top == "user" || top == "analysis" || top == "logs" || top == ".." || rel.has_root_path();
}

bool ApplyUpdate(const fs::path& unpacked, const fs::path& install, std::vector<std::string>* changed, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    std::error_code ec;
    fs::path src = unpacked;
    // A zip with one top folder (LBP3-Recompiled-<tag>\...): its contents.
    {
        int dirs = 0, files = 0;
        fs::path only;
        for (const auto& e : fs::directory_iterator(unpacked, ec)) {
            if (e.is_directory(ec)) ++dirs, only = e.path();
            else ++files;
        }
        if (dirs == 1 && files == 0) src = only;
    }
    if (!fs::exists(src / "LBP3Launcher.exe", ec) && !fs::exists(src / "lbp3.exe", ec))
        return fail("the release has no LBP3Launcher.exe or lbp3.exe; not installing it");
    for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) return fail("couldn't read the unpacked release");
        if (it->is_directory(ec)) continue;
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (ProtectedPath(rel)) continue;  // saves, settings, accounts, logs: never
        const fs::path dst = install / rel;
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            // In use (the running launcher, its SDL3.dll): renamed aside, then the new one copied in.
            const fs::path old = dst.string() + ".old";
            std::error_code ec2;
            fs::remove(old, ec2);
            fs::rename(dst, old, ec2);
            if (ec2) return fail("couldn't replace " + rel.string() + " (" + ec.message() + ")");
            std::ofstream(install / ".update_old_files.txt", std::ios::app) << rel.generic_string() << ".old\n";  // for RemoveOldFiles
            fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec2);
            if (ec2) {
                fs::rename(old, dst, ec2);  // put it back
                return fail("couldn't replace " + rel.string());
            }
        }
        if (changed) changed->push_back(rel.generic_string());
    }
    return true;
}

void RemoveOldFiles(const fs::path& install) {
    // Only the files an update recorded renaming aside (.update_old_files.txt), nothing else.
    const fs::path list = install / ".update_old_files.txt";
    std::ifstream in(list);
    if (!in) return;
    std::string line;
    bool left = false;
    std::vector<std::string> keep;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const fs::path rel = fs::u8path(line);
        if (line.empty() || rel.extension() != ".old" || ProtectedPath(rel)) continue;
        std::error_code ec;
        fs::remove(install / rel, ec);
        if (ec && fs::exists(install / rel)) left = true, keep.push_back(line);  // still in use: next time
    }
    in.close();
    std::error_code ec;
    if (!left) fs::remove(list, ec);
    else {
        std::ofstream out(list, std::ios::trunc);
        for (const auto& k : keep) out << k << "\n";
    }
}

std::vector<std::string> ReleaseHighlights(const std::string& markdown) {
    auto clean = [](const std::string& s) {
        std::string r;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '`') continue;
            if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') {
                ++i;
                continue;
            }
            r += s[i];
        }
        while (!r.empty() && std::isspace(static_cast<unsigned char>(r.back()))) r.pop_back();
        return r;
    };
    std::vector<std::string> out;
    std::istringstream in(markdown);
    std::string line;
    bool inside = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("#", 0) == 0) {
            if (inside) break;  // the next heading ends the section
            std::string lower = line;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            inside = lower.find("what's in it") != std::string::npos;
            continue;
        }
        if (!inside) continue;
        const size_t text = line.find_first_not_of(' ');
        if (line.rfind("- ", 0) == 0) out.push_back(clean(line.substr(2)));
        else if (!out.empty() && text != std::string::npos && text > 0) out.back() += " " + clean(line.substr(text));
    }
    return out;
}

std::vector<std::string> ReadReleaseHighlights(const fs::path& launcher_dir) {
    std::ifstream f(launcher_dir / "RELEASE-NOTES.md", std::ios::binary);
    if (!f) return {};
    std::stringstream s;
    s << f.rdbuf();
    return ReleaseHighlights(s.str());
}

}  // namespace launcher
