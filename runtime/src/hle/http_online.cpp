// SPDX-License-Identifier: GPL-2.0-or-later
// libSceHttp for online play (hle/online.h): requests really go out, over WinHTTP, to the
// configured community server. Off by default; hle/http_offline.cpp is used then.
//
// The game's requests keep their path and query; only the scheme, host and port become the
// configured server's (LBP3's own is https://littlebigplanetps3.online.scee.com:10061). Cookies
// are kept here (the game's MM_AUTH session cookie) rather than by WinHTTP, so they are visible
// for signing the answers below.
//
// Adapted from LBPPC (emurphy04, https://github.com/emurphy04/LBPPC at aad6a91),
// gpu/bbnet/vendor/shadps4/core/libraries/network/http.cpp (SPDX GPL-2.0-or-later, Copyright
// 2024-2026 shadPS4 Emulator Project, with LBPPC's "lbppc:" changes): BuildLocalLoginTicket (the
// PS3-style NP ticket the login carries) and the answers for what Refresh doesn't serve the PS4
// game (/farc_hashes and /t_conf 410 -> empty signed 200; /npdata's empty reply -> the friends
// posted; a co-op guest's /match 401 -> signed success), signed as the server signs its replies
// (X-Digest-A: HMAC-SHA1 with "CustomServerDigest" over body + MM_AUTH cookie + path).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include "hle/kernel_errors.h"
#include "hle/online.h"
#include "hle/register.h"

namespace hle::http_online {
namespace {

constexpr int kErrorInvalidId = static_cast<int>(0x80431100);
constexpr int kErrorInvalidValue = static_cast<int>(0x804311fe);
constexpr int kErrorNetwork = static_cast<int>(0x80431063);
constexpr int kErrorBeforeSend = static_cast<int>(0x80431065);
constexpr int kErrorParseNotFound = static_cast<int>(0x80432025);
constexpr int kECONNREFUSED = 61;

const char* const kMethods[] = {"GET", "POST", "HEAD", "OPTIONS", "PUT", "DELETE", "TRACE", "CONNECT"};

struct Header {
    std::string name, value;
};

struct Request {
    int conn = 0;
    std::string method = "GET";
    std::string url;  // as the game gave it
    std::string userAgent;  // the template's
    std::vector<Header> headers;
    uint64_t contentLength = 0;
    bool sent = false;
    int status = 0;
    std::string rawHeaders;  // "HTTP/1.1 200 OK\r\nName: value\r\n...\r\n"
    std::vector<uint8_t> body;
    size_t readPos = 0;
    int lastErrno = 0;
};

struct Connection {
    int tmpl = 0;
    std::string base;  // "scheme://host:port" the game named (only the path matters)
};

std::mutex g_mutex;
std::atomic<int> g_nextId{1};
std::map<int, std::string> g_templates;  // id -> user agent
std::map<int, Connection> g_connections;
std::map<int, std::shared_ptr<Request>> g_requests;
std::string g_cookie;  // "MM_AUTH=..." once the server set it

HINTERNET Session() {
    static HINTERNET s = [] {
        HINTERNET h = WinHttpOpen(L"LBP3 native (online)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) WinHttpSetTimeouts(h, 10000, 10000, 15000, 30000);
        return h;
    }();
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string Narrow(const wchar_t* w, size_t len) {
    if (!len) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), s.data(), n, nullptr, nullptr);
    return s;
}

bool IEquals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// The path and query of a URL ("/LITTLEBIGPLANETPS3_XML/login?..." from "https://host:port/...").
std::string PathOf(const std::string& url) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos) return url.empty() || url[0] != '/' ? "/" + url : url;
    const auto slash = url.find('/', scheme + 3);
    return slash == std::string::npos ? "/" : url.substr(slash);
}

bool PathEndsWith(const std::string& path, std::string_view tail) {
    return path.size() >= tail.size() && path.compare(path.size() - tail.size(), tail.size(), tail) == 0;
}

std::string HmacSha1Hex(std::string_view key, std::string_view data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char out[20] = {};
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
        return hex;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, (PUCHAR)key.data(), static_cast<ULONG>(key.size()), 0) == 0) {
        BCryptHashData(hash, (PUCHAR)data.data(), static_cast<ULONG>(data.size()), 0);
        BCryptFinishHash(hash, out, sizeof(out), 0);
        BCryptDestroyHash(hash);
        char buf[3];
        for (unsigned char b : out) {
            std::snprintf(buf, sizeof(buf), "%02x", b);
            hex += buf;
        }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

// --- Adapted from LBPPC's http.cpp (GPL-2.0-or-later; see the file comment) ----------------

// A PS3 NP ticket (version 2.1: big-endian typed fields in a body section and a footer
// section) for the locally signed-in player, as LittleBigPlanet 3 (BCUS98362) on PS3. It isn't
// signed: a custom server accepts it with ticket verification off (a private server).
std::vector<uint8_t> BuildLocalLoginTicket(const std::string& name) {
    std::vector<uint8_t> out;
    const auto u16be = [&](uint16_t v) {
        out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v));
    };
    const auto u32be = [&](uint32_t v) {
        u16be(uint16_t(v >> 16));
        u16be(uint16_t(v));
    };
    const auto u64be = [&](uint64_t v) {
        u32be(uint32_t(v >> 32));
        u32be(uint32_t(v));
    };
    const auto field = [&](uint16_t type, std::string_view bytes, size_t length) {
        u16be(type);
        u16be(uint16_t(length));
        for (size_t i = 0; i < length; ++i) out.push_back(i < bytes.size() ? uint8_t(bytes[i]) : 0);
    };
    const uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count());
    uint64_t userId = 0xcbf29ce484222325ull;  // FNV-1a of the name, as the local account id
    for (const char c : name) userId = (userId ^ uint8_t(c)) * 0x100000001b3ull;

    std::vector<uint8_t> body;
    std::swap(out, body);
    field(8, "lbp3-local-ticket", 0x14);  // serial
    u16be(1), u16be(4), u32be(0x100);     // issuer: PSN (PS3)
    u16be(7), u16be(8), u64be(now);       // issued
    u16be(7), u16be(8), u64be(now + 4ull * 3600 * 1000);  // expires
    u16be(2), u16be(8), u64be(userId);
    field(4, name, 0x20);                    // online id
    field(8, "us", 4);                       // country
    field(4, "un", 4);                       // domain
    field(8, "UP9000-BCUS98362_00", 0x18);   // service id: LittleBigPlanet 3 (US)
    u16be(1), u16be(4), u32be(0);            // status
    u16be(0), u16be(0), u16be(0), u16be(0);
    std::swap(out, body);

    std::vector<uint8_t> footer;
    std::swap(out, footer);
    field(8, "LBPC", 4);                     // signature identifier
    field(8, std::string_view{}, 0x38);      // signature (none)
    std::swap(out, footer);

    std::vector<uint8_t> sections;
    std::swap(out, sections);
    out.push_back(0x30), out.push_back(0x00), u16be(uint16_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(0x30), out.push_back(0x02), u16be(uint16_t(footer.size()));
    out.insert(out.end(), footer.begin(), footer.end());
    std::swap(out, sections);

    out = {0x21, 0x01, 0, 0, 0, 0};
    u16be(uint16_t(sections.size()));
    out.insert(out.end(), sections.begin(), sections.end());
    return out;
}

// The reply signed as the server signs its replies, which the game checks. The cookie is the
// MM_AUTH value the request carried: the game sends it itself (from the login reply's
// authTicket), and a real server sets no cookie of its own, so ours may be empty.
void AnswerSigned(Request& r, const std::string& path, const std::string& body,
                  std::string_view contentType = "text/xml") {
    std::string jar;
    for (const Header& h : r.headers)
        if (IEquals(h.name, "Cookie")) jar = h.value;
    if (jar.find("MM_AUTH=") == std::string::npos) {
        std::lock_guard lk(g_mutex);
        jar = g_cookie;
    }
    std::string cookie;
    if (const auto at = jar.find("MM_AUTH="); at != std::string::npos) {
        cookie = jar.substr(at + 8);
        cookie = cookie.substr(0, cookie.find(';'));
    }
    const std::string hex = HmacSha1Hex("CustomServerDigest", body + cookie + path);
    r.status = 200;
    r.body.assign(body.begin(), body.end());
    r.readPos = 0;
    char head[512];
    std::snprintf(head, sizeof(head),
                  "HTTP/1.1 200 OK\r\nContent-Type: %.*s\r\nContent-Length: %zu\r\nX-Digest-A: %s\r\n\r\n",
                  static_cast<int>(contentType.size()), contentType.data(), body.size(), hex.c_str());
    r.rawHeaders = head;
}

// Endpoints a Refresh-style server built for the PS3 game doesn't serve the PS4 game.
void FixUpReply(Request& r, const std::string& pathAndQuery, const std::vector<uint8_t>& posted) {
    const std::string path = pathAndQuery.substr(0, pathAndQuery.find('?'));
    if (r.status == 410 && (PathEndsWith(path, "/farc_hashes") || PathEndsWith(path, "/t_conf"))) {
        AnswerSigned(r, path, PathEndsWith(path, "/farc_hashes") ? "<farc_hashes></farc_hashes>" : "");
        std::fprintf(stderr, "[online] %s: the server's 410 answered as an empty signed 200\n", path.c_str());
    } else if (r.status == 200 && r.body.empty() && PathEndsWith(path, "/npdata")) {
        const std::string sent(posted.begin(), posted.end());
        const auto b = sent.find("<friends>"), e = sent.find("</friends>");
        if (b != std::string::npos && e != std::string::npos && e > b) {
            AnswerSigned(r, path, "<npdata>" + sent.substr(b, e + 10 - b) + "</npdata>");
            std::fprintf(stderr, "[online] %s: answered with the friends posted (signed)\n", path.c_str());
        }
    } else if (r.status == 401 && PathEndsWith(path, "/match")) {
        AnswerSigned(r, path, "[{\"StatusCode\":200}]", "application/json");
        static std::atomic<int> n{0};
        if (n++ < 5) std::fprintf(stderr, "[online] %s: a co-op guest's 401 answered as a signed success\n", path.c_str());
    }
}

// --- End of the adapted part ---------------------------------------------------------------

// LBP3_ONLINE_LOG=1: every request and reply in full (headers, the first 512 bytes of each
// body), with session cookies redacted. For finding out what a server and the game disagree on.
bool VerboseLog() {
    static const bool on = [] {
        const char* v = std::getenv("LBP3_ONLINE_LOG");
        return v && *v && *v != '0';
    }();
    return on;
}

// "MM_AUTH=<value>" -> "MM_AUTH=<redacted:N>", anywhere in the text.
std::string Redact(std::string s) {
    size_t at = 0;
    while ((at = s.find("MM_AUTH=", at)) != std::string::npos) {
        at += 8;
        size_t end = at;
        while (end < s.size() && s[end] != ';' && s[end] != '\r' && s[end] != '\n' && s[end] != '<' &&
               s[end] != '"' && s[end] != ' ')
            ++end;
        const std::string tag = "<redacted:" + std::to_string(end - at) + ">";
        s.replace(at, end - at, tag);
        at += tag.size();
    }
    return s;
}

std::string Preview(const uint8_t* data, size_t size) {
    const size_t n = std::min<size_t>(size, 512);
    bool text = true;
    for (size_t i = 0; i < n && text; ++i)
        text = (data[i] >= 32 && data[i] < 127) || data[i] == '\r' || data[i] == '\n' || data[i] == '\t';
    std::string out;
    if (text) {
        out.assign(reinterpret_cast<const char*>(data), n);
    } else {
        char b[4];
        for (size_t i = 0; i < std::min<size_t>(n, 128); ++i) {
            std::snprintf(b, sizeof(b), "%02x", data[i]);
            out += b;
        }
        out = "(binary) " + out;
    }
    if (size > n) out += "...(" + std::to_string(size) + " bytes)";
    return Redact(out);
}

void RememberCookies(const std::string& rawHeaders) {
    size_t at = 0;
    while ((at = rawHeaders.find("\r\n", at)) != std::string::npos) {
        at += 2;
        const auto end = rawHeaders.find("\r\n", at);
        const std::string line = rawHeaders.substr(at, end == std::string::npos ? std::string::npos : end - at);
        const auto colon = line.find(':');
        if (colon == std::string::npos || !IEquals(std::string_view(line).substr(0, colon), "Set-Cookie")) continue;
        std::string v = line.substr(colon + 1);
        v.erase(0, v.find_first_not_of(' '));
        v = v.substr(0, v.find(';'));
        if (v.rfind("MM_AUTH=", 0) == 0) {
            std::lock_guard lk(g_mutex);
            g_cookie = v;
        }
    }
}

// The body reaches the game whole and decoded: drop Content-Encoding / Transfer-Encoding and
// give the real Content-Length, when the server used either (otherwise the headers are untouched).
void NormalizeHeaders(Request& r) {
    std::string out, line;
    bool changed = false;
    size_t at = 0;
    while (at < r.rawHeaders.size()) {
        size_t end = r.rawHeaders.find("\r\n", at);
        if (end == std::string::npos) end = r.rawHeaders.size();
        line = r.rawHeaders.substr(at, end - at);
        at = end + 2;
        if (line.empty()) continue;
        const auto colon = line.find(':');
        const std::string_view name = colon == std::string::npos ? std::string_view{} : std::string_view(line).substr(0, colon);
        if (IEquals(name, "Content-Encoding") || IEquals(name, "Transfer-Encoding")) {
            changed = true;
            continue;
        }
        out += line + "\r\n";
    }
    if (!changed) return;
    std::string kept;
    at = 0;
    while (at < out.size()) {  // drop the old Content-Length too, then add the real one
        size_t end = out.find("\r\n", at);
        line = out.substr(at, end - at);
        at = end + 2;
        const auto colon = line.find(':');
        if (colon != std::string::npos && IEquals(std::string_view(line).substr(0, colon), "Content-Length")) continue;
        kept += line + "\r\n";
    }
    r.rawHeaders = kept + "Content-Length: " + std::to_string(r.body.size()) + "\r\n\r\n";
}

int Send(Request& r, const std::vector<uint8_t>& postIn) {
    const online::Server& srv = online::GetServer();
    const std::string path = PathOf(r.url);
    std::vector<uint8_t> post = postIn;
    if (r.method == "POST" && path.find("/login?") != std::string::npos) {
        post = BuildLocalLoginTicket(online::Name());
        std::fprintf(stderr, "[online] login: a %zu-byte ticket for '%s'\n", post.size(), online::Name().c_str());
    }
    HINTERNET ses = Session();
    if (!ses) return kErrorNetwork;
    HINTERNET con = WinHttpConnect(ses, Wide(srv.host).c_str(), srv.port, 0);
    if (!con) {
        r.lastErrno = kECONNREFUSED;
        return kErrorNetwork;
    }
    HINTERNET req = WinHttpOpenRequest(con, Wide(r.method).c_str(), Wide(path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, srv.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(con);
        r.lastErrno = kECONNREFUSED;
        return kErrorNetwork;
    }
    DWORD noCookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &noCookies, sizeof(noCookies));
    std::string headers;
    bool haveCookie = false, haveAgent = false;
    for (const Header& h : r.headers) {
        if (IEquals(h.name, "Host") || IEquals(h.name, "Content-Length")) continue;
        // The game accepts gzip/deflate, which libSceHttp inflates for it before it reads the
        // body. Bodies are asked for uncompressed instead: a compressed /searches reply passed
        // through raw (or empty, through WinHTTP's decompression) kept the game from going online.
        if (IEquals(h.name, "Accept-Encoding")) continue;
        if (IEquals(h.name, "Cookie")) haveCookie = true;
        if (IEquals(h.name, "User-Agent")) haveAgent = true;
        headers += h.name + ": " + h.value + "\r\n";
    }
    // Like libSceHttp: the template's user agent unless the request names its own (servers
    // tell the game and its platform from it).
    if (!haveAgent && !r.userAgent.empty()) headers += "User-Agent: " + r.userAgent + "\r\n";
    {
        std::lock_guard lk(g_mutex);
        if (!haveCookie && !g_cookie.empty()) headers += "Cookie: " + g_cookie + "\r\n";
    }
    if (VerboseLog())
        std::fprintf(stderr, "[online-log] > %s %s://%s:%u%s\n%s> body: %s\n", r.method.c_str(), srv.scheme.c_str(),
                     srv.host.c_str(), srv.port, path.c_str(), Redact(headers).c_str(),
                     Preview(post.data(), post.size()).c_str());
    const std::wstring wh = Wide(headers);
    const BOOL sent = WinHttpSendRequest(req, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(),
                                         wh.empty() ? 0 : static_cast<DWORD>(-1L),
                                         post.empty() ? WINHTTP_NO_REQUEST_DATA : post.data(),
                                         static_cast<DWORD>(post.size()), static_cast<DWORD>(post.size()), 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        const DWORD err = GetLastError();
        std::fprintf(stderr, "[online] %s %s: no answer from the server (WinHTTP error %lu)\n", r.method.c_str(),
                     path.c_str(), err);
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        r.lastErrno = kECONNREFUSED;
        return kErrorNetwork;
    }
    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    DWORD hbytes = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                        &hbytes, WINHTTP_NO_HEADER_INDEX);
    std::wstring wraw(hbytes / sizeof(wchar_t), L'\0');
    if (hbytes &&
        WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, wraw.data(), &hbytes,
                            WINHTTP_NO_HEADER_INDEX)) {
        wraw.resize(hbytes / sizeof(wchar_t));
        while (!wraw.empty() && wraw.back() == L'\0') wraw.pop_back();
    }
    r.rawHeaders = Narrow(wraw.data(), wraw.size());
    r.status = static_cast<int>(status);
    r.body.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        const size_t at = r.body.size();
        r.body.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, r.body.data() + at, avail, &got)) {
            r.body.resize(at);
            break;
        }
        r.body.resize(at + got);
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    r.readPos = 0;
    RememberCookies(r.rawHeaders);
    NormalizeHeaders(r);
    if (VerboseLog())
        std::fprintf(stderr, "[online-log] < %d for %s\n%s< body: %s\n", r.status, path.c_str(),
                     Redact(r.rawHeaders).c_str(), Preview(r.body.data(), r.body.size()).c_str());
    FixUpReply(r, path, post);
    static std::atomic<int> logged{0};
    if (logged++ < 400)
        std::fprintf(stderr, "[online] %s %s -> %d, %zu bytes\n", r.method.c_str(), path.c_str(), r.status,
                     r.body.size());
    return kernel::SCE_OK;
}

std::shared_ptr<Request> Find(int id) {
    std::lock_guard lk(g_mutex);
    auto it = g_requests.find(id);
    return it == g_requests.end() ? nullptr : it->second;
}

int NewId() { return g_nextId++; }

}  // namespace

int PS4_SYSV_ABI sceHttpInit(int, int, size_t) { return NewId(); }
int PS4_SYSV_ABI sceHttpTerm(int) { return kernel::SCE_OK; }

int PS4_SYSV_ABI sceHttpCreateTemplate(int, const char* userAgent, int, int) {
    const int id = NewId();
    std::lock_guard lk(g_mutex);
    g_templates[id] = userAgent ? userAgent : "";
    return id;
}
int PS4_SYSV_ABI sceHttpDeleteTemplate(int id) {
    std::lock_guard lk(g_mutex);
    g_templates.erase(id);
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpCreateConnection(int tmpl, const char* host, const char* scheme, uint16_t port, int) {
    const int id = NewId();
    std::lock_guard lk(g_mutex);
    g_connections[id] = Connection{tmpl, std::string(scheme ? scheme : "http") + "://" + (host ? host : "") + ":" +
                                             std::to_string(port)};
    return id;
}
int PS4_SYSV_ABI sceHttpCreateConnectionWithURL(int tmpl, const char* url, int) {
    const int id = NewId();
    std::lock_guard lk(g_mutex);
    g_connections[id] = Connection{tmpl, url ? url : ""};
    return id;
}
int PS4_SYSV_ABI sceHttpDeleteConnection(int id) {
    std::lock_guard lk(g_mutex);
    g_connections.erase(id);
    return kernel::SCE_OK;
}

static int NewRequest(int conn, int method, const std::string& url, uint64_t contentLength) {
    auto r = std::make_shared<Request>();
    r->conn = conn;
    r->method = method >= 0 && method < 8 ? kMethods[method] : "GET";
    r->url = url;
    r->contentLength = contentLength;
    const int id = NewId();
    std::lock_guard lk(g_mutex);
    if (auto c = g_connections.find(conn); c != g_connections.end())
        if (auto t = g_templates.find(c->second.tmpl); t != g_templates.end()) r->userAgent = t->second;
    g_requests[id] = std::move(r);
    return id;
}
int PS4_SYSV_ABI sceHttpCreateRequest(int conn, int method, const char* path, uint64_t contentLength) {
    std::string base;
    {
        std::lock_guard lk(g_mutex);
        if (auto it = g_connections.find(conn); it != g_connections.end()) base = it->second.base;
    }
    return NewRequest(conn, method, base + (path ? path : "/"), contentLength);
}
int PS4_SYSV_ABI sceHttpCreateRequestWithURL(int conn, int method, const char* url, uint64_t contentLength) {
    return NewRequest(conn, method, url ? url : "/", contentLength);
}
int PS4_SYSV_ABI sceHttpDeleteRequest(int id) {
    std::lock_guard lk(g_mutex);
    g_requests.erase(id);
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpAbortRequest(int) { return kernel::SCE_OK; }

int PS4_SYSV_ABI sceHttpSendRequest(int id, const void* post, size_t size) {
    auto r = Find(id);
    if (!r) return kErrorInvalidId;
    std::vector<uint8_t> body;
    if (post && size) body.assign(static_cast<const uint8_t*>(post), static_cast<const uint8_t*>(post) + size);
    const int rc = Send(*r, body);
    r->sent = rc == kernel::SCE_OK;
    return rc;
}

int PS4_SYSV_ABI sceHttpGetStatusCode(int id, int* status) {
    auto r = Find(id);
    if (!r) return kErrorInvalidId;
    if (!r->sent) return kErrorBeforeSend;
    if (status) *status = r->status;
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpGetResponseContentLength(int id, int* result, uint64_t* length) {
    auto r = Find(id);
    if (!r) return kErrorInvalidId;
    if (!r->sent) return kErrorBeforeSend;
    if (!result || !length) return kErrorInvalidValue;
    *result = 0;  // known
    *length = r->body.size();
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpGetAllResponseHeaders(int id, char** header, size_t* size) {
    auto r = Find(id);
    if (!r) return kErrorInvalidId;
    if (!r->sent) return kErrorBeforeSend;
    if (header) *header = r->rawHeaders.data();
    if (size) *size = r->rawHeaders.size();
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpParseResponseHeader(const char* header, size_t len, const char* field, const char** value,
                                            size_t* valueLen) {
    if (!header || !field || !value || !valueLen) return kErrorInvalidValue;
    const std::string_view all(header, len);
    const std::string_view name(field);
    size_t at = 0;
    while (at < all.size()) {
        size_t end = all.find("\r\n", at);
        if (end == std::string_view::npos) end = all.size();
        const std::string_view line = all.substr(at, end - at);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos && IEquals(line.substr(0, colon), name)) {
            size_t v = colon + 1;
            while (v < line.size() && (line[v] == ' ' || line[v] == '\t')) ++v;
            *value = header + at + v;
            *valueLen = line.size() - v;
            return static_cast<int>(*valueLen);
        }
        at = end + 2;
    }
    return kErrorParseNotFound;
}
int PS4_SYSV_ABI sceHttpReadData(int id, void* data, size_t size) {
    auto r = Find(id);
    if (!r) return kErrorInvalidId;
    if (!r->sent) return kErrorBeforeSend;
    const size_t n = std::min(size, r->body.size() - std::min(r->readPos, r->body.size()));
    if (n && data) std::memcpy(data, r->body.data() + r->readPos, n);
    r->readPos += n;
    return static_cast<int>(n);
}
int PS4_SYSV_ABI sceHttpGetLastErrno(int id, int* err) {
    auto r = Find(id);
    if (err) *err = r ? r->lastErrno : 0;
    return kernel::SCE_OK;
}

int PS4_SYSV_ABI sceHttpAddRequestHeader(int id, const char* name, const char* value, int mode) {
    if (!name) return kErrorInvalidValue;
    auto r = Find(id);
    if (!r) return kernel::SCE_OK;  // headers on templates/connections: not needed here
    std::lock_guard lk(g_mutex);
    if (mode == 0)  // overwrite
        std::erase_if(r->headers, [&](const Header& h) { return IEquals(h.name, name); });
    r->headers.push_back({name, value ? value : ""});
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpRemoveRequestHeader(int id, const char* name) {
    auto r = Find(id);
    if (r && name) {
        std::lock_guard lk(g_mutex);
        std::erase_if(r->headers, [&](const Header& h) { return IEquals(h.name, name); });
    }
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpSetRequestContentLength(int id, uint64_t len) {
    if (auto r = Find(id)) r->contentLength = len;
    return kernel::SCE_OK;
}

// Settings the WinHTTP session covers itself.
int PS4_SYSV_ABI sceHttpSetAutoRedirect(int, int) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetAuthEnabled(int, int) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetCookieEnabled(int, int) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetConnectTimeOut(int, uint32_t) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetSendTimeOut(int, uint32_t) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetRecvTimeOut(int, uint32_t) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetRedirectCallback(int, void*, void*) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetCookieRecvCallback(int, void*, void*) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpSetCookieSendCallback(int, void*, void*) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpsSetSslCallback(int, void*, void*) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpsLoadCert(int, int, const void*, const void*, const void*) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpsUnloadCert(int) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpAddCookie(int, const char*, const char*, size_t) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpCookieFlush(int) { return kernel::SCE_OK; }
int PS4_SYSV_ABI sceHttpGetAutoRedirect(int, int* enabled) {
    if (enabled) *enabled = 1;
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpGetCookieEnabled(int, int* enabled) {
    if (enabled) *enabled = 1;
    return kernel::SCE_OK;
}
int PS4_SYSV_ABI sceHttpGetAuthEnabled(int, int* enabled) {
    if (enabled) *enabled = 0;
    return kernel::SCE_OK;
}

// For the offline test: the ticket and the signing, without a game.
std::vector<uint8_t> TestTicket(const std::string& name) { return BuildLocalLoginTicket(name); }
std::string TestHmac(const std::string& key, const std::string& data) { return HmacSha1Hex(key, data); }

}  // namespace hle::http_online

namespace hle {

// Replaces the offline libSceHttp entries (registered after them) when online play is on.
void RegisterHttpOnline(core::Linker& linker) {
    if (!online::Enabled()) return;
    using namespace http_online;
    const char* h = "libSceHttp";
    HLE_REG(linker, h, sceHttpInit);
    HLE_REG(linker, h, sceHttpTerm);
    HLE_REG(linker, h, sceHttpCreateTemplate);
    HLE_REG(linker, h, sceHttpDeleteTemplate);
    HLE_REG(linker, h, sceHttpCreateConnection);
    HLE_REG(linker, h, sceHttpCreateConnectionWithURL);
    HLE_REG(linker, h, sceHttpDeleteConnection);
    HLE_REG(linker, h, sceHttpCreateRequest);
    HLE_REG(linker, h, sceHttpCreateRequestWithURL);
    HLE_REG(linker, h, sceHttpDeleteRequest);
    HLE_REG(linker, h, sceHttpAbortRequest);
    HLE_REG(linker, h, sceHttpSendRequest);
    HLE_REG(linker, h, sceHttpGetStatusCode);
    HLE_REG(linker, h, sceHttpGetResponseContentLength);
    HLE_REG(linker, h, sceHttpGetAllResponseHeaders);
    HLE_REG(linker, h, sceHttpParseResponseHeader);
    HLE_REG(linker, h, sceHttpReadData);
    HLE_REG(linker, h, sceHttpGetLastErrno);
    HLE_REG(linker, h, sceHttpAddRequestHeader);
    HLE_REG(linker, h, sceHttpRemoveRequestHeader);
    HLE_REG(linker, h, sceHttpSetRequestContentLength);
    HLE_REG(linker, h, sceHttpSetAutoRedirect);
    HLE_REG(linker, h, sceHttpSetAuthEnabled);
    HLE_REG(linker, h, sceHttpSetCookieEnabled);
    HLE_REG(linker, h, sceHttpSetConnectTimeOut);
    HLE_REG(linker, h, sceHttpSetSendTimeOut);
    HLE_REG(linker, h, sceHttpSetRecvTimeOut);
    HLE_REG(linker, h, sceHttpSetRedirectCallback);
    HLE_REG(linker, h, sceHttpSetCookieRecvCallback);
    HLE_REG(linker, h, sceHttpSetCookieSendCallback);
    HLE_REG(linker, h, sceHttpsSetSslCallback);
    HLE_REG(linker, h, sceHttpsLoadCert);
    HLE_REG(linker, h, sceHttpsUnloadCert);
    HLE_REG(linker, h, sceHttpAddCookie);
    HLE_REG(linker, h, sceHttpCookieFlush);
    HLE_REG(linker, h, sceHttpGetAutoRedirect);
    HLE_REG(linker, h, sceHttpGetCookieEnabled);
    HLE_REG(linker, h, sceHttpGetAuthEnabled);
}

}  // namespace hle
